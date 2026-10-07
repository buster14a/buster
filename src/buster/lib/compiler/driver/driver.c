// The Clang-like `ide cc` driver: two public calls (driver.h) split
// argument parsing from execution. compiler_driver_parse_arguments turns
// argv into a CompilerDriverInvocation — dialect, target/CPU/features,
// inputs classified as C source, objects, or archives, their ordered library
// occurrences through compiler_driver_link_operation, and every output
// mode — rejecting unknown languages and retired options explicitly
// rather than guessing. compiler_driver_validate_request is the request
// combination authority both calls share, so an API-built invocation is refused
// exactly as its argv spelling would be. The parser first replaces `@path` arguments through
// compiler_driver_expand_response_files (bounded, one level, no nesting).
// Comma-separated -Wl payloads become individual linker arguments;
// link_validate_linker_arguments owns their supported semantic subset.
// compiler_driver_execute_invocation then runs the
// selected pipeline: compiler_driver_execute_c_single carries a C input
// through preprocess, parse, lowering, codegen, and object/executable
// output (with -emit-llvm, WebAssembly, eBPF, and direct Vulkan compute
// SPIR-V as alternate canonical emissions), the
// compiler_driver_preprocess_text serializer keeps -E line structure while
// guarding every apparent adjacency with the C lexical-boundary rules, the
// dynamic-library plumbing around compiler_driver_dynamic_libraries
// resolves import libraries for hosted links — reading a library's own
// export table when one is needed, through
// compiler_driver_pe_library_exports on Windows and
// compiler_driver_elf_library_exports on Linux — and
// compiler_driver_execute_gpu isolates the external shader-toolchain
// orchestration in gpu.h. compiler_prewarm at the top fills every lazily
// built compile-path table on one thread before parallel lanes run
// (AGENTS.md). CompilerDriverDiagnosticCollector and the private adapters in
// driver_diagnostic.c publish stable records into the result arena;
// compiler_driver_publish_c_diagnostics preserves producer/stage ordering.
// Optional fallback_records retain source/function attribution across TU arena
// destruction; no per-function recording is allocated in ordinary compilation.
// compiler_driver_finish_investigation publishes an optional single-function
// capture only after binding retained machine bytes to the completed object.
// compiler_driver_unit_lane fills one private TU arena/collector per stable
// input slot; the coordinator creates and destroys those arenas, so the
// per-thread arena pool circulates them. Opt-in native C link batches publish
// in input order only after the gang returns; assembly and archive selection
// remain serial boundaries.
// Every unit enters through compiler_driver_execute_unit. With
// collect_input_metrics, compiler_driver_prime_arenas and the prewarm run
// first, CompilerDriverUnitMetrics (compiler_driver_phase_begin marks the
// CompilerDriverPhase boundaries as offsets from metrics_origin) and
// CompilerDriverArenaWatch measure each unit, and compiler_driver_input_record
// publishes one CompilerDriverInputResult per input with its diagnostic
// digest; compiler_driver_fail_input records failures outside a unit, and
// -fkeep-going continues past them. compiler_driver_metrics_format at the end
// renders the -fmetrics-out records (schema: docs/agents/driver.md). A null
// metrics pointer is the ordinary compile: no clock reads.
// The test-only compiler_driver_test_setup_order observer checks completed
// setup against real serial unit boundaries without comparing wall times.
// archive.c owns indexed archive extraction and its pass-ordered worklist.
// compiler_driver_elf_library_roots shares target/sysroot search roots between
// export discovery and static-library lookup; explicit -L roots come first.
// compiler_driver_elf_linker_script classifies located requested scripts for
// explicit refusal; no script contents are evaluated or skipped as absent.
// compiler_driver_elf_compiler_runtime adds existing libgcc_s on demand for
// unresolved half/quad helper calls after explicit library exports are known.
// compiler_driver_publish_slices preserves atomic artifacts and write failures;
// execute_invocation normalizes textual -o - before choosing a pipeline.
// A lone `-` input is admitted by the parser only as C source; execute_c_single
// reads its text from CompilerDriverInvocation.standard_input.

// compiler_driver_elf_shared_is_incompatible keeps alien shared candidates
// from hiding usable archives during that ordered search.

#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/driver/driver_internal.h>
#include <buster/lib/compiler/driver/codegen_configurations.h>

#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/codegen/bootstrap_trace.h>
#include <buster/lib/compiler/codegen/investigation.h>
#include <buster/lib/byte_writer.h>
#include <buster/lib/time.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_semantics.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/work_ledger.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/time.h>
#include <buster/lib/hash.h>
#include <buster/lib/compiler/driver/archive.c>

void compiler_prewarm(void)
{
    c_prewarm();
    codegen_prewarm();
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_target_is_wasm(Target target)
{
    return target.cpu_arch == CPU_ARCH_WASM32 || target.cpu_arch == CPU_ARCH_WASM64;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_target_is_supported_wasm(Target target)
{
    return (target.cpu_arch == CPU_ARCH_WASM32 && target.os == OPERATING_SYSTEM_WASI) ||
           (target.cpu_arch == CPU_ARCH_WASM64 && target.os == OPERATING_SYSTEM_FREESTANDING);
}

BUSTER_GLOBAL_LOCAL WasmOptions compiler_driver_wasm_options(Target target)
{
    return target.cpu_arch == CPU_ARCH_WASM32 ? WASM32_WASI_OPTIONS_DEFAULT : WASM64_OPTIONS_DEFAULT;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_windows_runtime_object_target(Target target)
{
    return target.os == OPERATING_SYSTEM_WINDOWS && (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64);
}

// The ELF runtime object carries the glibc stubs that live in
// libc_nonshared.a rather than in the shared object the ELF writers import
// from; see link_elf_libc_runtime_object.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_runtime_object_target(Target target)
{
    return target.os == OPERATING_SYSTEM_LINUX && (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64);
}

// How many synthetic runtime objects a hosted link for this target can add:
// Windows takes the unconditional `_fltused` marker plus the UCRT exit-handler
// stubs, ELF only the glibc ones.  Both stub objects are selected the way an
// archive member is, so this is the capacity to reserve, not the count that
// will be used.
BUSTER_GLOBAL_LOCAL u32 compiler_driver_runtime_object_capacity(Target target)
{
    return compiler_driver_windows_runtime_object_target(target) ? 2 : compiler_driver_elf_runtime_object_target(target) ? 1 : 0;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_option_value(String8 argument, String8 prefix)
{
    if (!string_starts_with_sequence(argument, prefix))
    {
        return (String8){0};
    }
    return (String8){
        .pointer = argument.pointer + prefix.length,
        .length = argument.length - prefix.length,
    };
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_dialect(CompilerDriverInvocation* invocation, String8 dialect)
{
    if (string_equal(dialect, S8("gnu89")) || string_equal(dialect, S8("gnu90")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU89;
    }
    else if (string_equal(dialect, S8("gnu99")) || string_equal(dialect, S8("gnu9x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU99;
    }
    else if (string_equal(dialect, S8("gnu11")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU11;
    }
    else if (string_equal(dialect, S8("gnu17")) || string_equal(dialect, S8("gnu18")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU17;
    }
    else if (string_equal(dialect, S8("gnu23")) || string_equal(dialect, S8("gnu2x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU23;
    }
    else if (string_equal(dialect, S8("c99")) || string_equal(dialect, S8("c9x")) || string_equal(dialect, S8("iso9899:1999")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C99;
    }
    else if (string_equal(dialect, S8("c11")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C11;
    }
    else if (string_equal(dialect, S8("c17")) || string_equal(dialect, S8("c18")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C17;
    }
    else if (string_equal(dialect, S8("c23")) || string_equal(dialect, S8("c2x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C23;
    }
    else
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL CPreprocessDialect compiler_driver_preprocess_dialect(CompilerDriverCDialect dialect)
{
    switch (dialect)
    {
    case COMPILER_DRIVER_C_DIALECT_GNU99:
        return C_PREPROCESS_DIALECT_GNU99;
    case COMPILER_DRIVER_C_DIALECT_GNU11:
        return C_PREPROCESS_DIALECT_GNU11;
    case COMPILER_DRIVER_C_DIALECT_GNU17:
        return C_PREPROCESS_DIALECT_GNU17;
    case COMPILER_DRIVER_C_DIALECT_GNU23:
        return C_PREPROCESS_DIALECT_GNU23;
    case COMPILER_DRIVER_C_DIALECT_C99:
        return C_PREPROCESS_DIALECT_C99;
    case COMPILER_DRIVER_C_DIALECT_C11:
        return C_PREPROCESS_DIALECT_C11;
    case COMPILER_DRIVER_C_DIALECT_C17:
        return C_PREPROCESS_DIALECT_C17;
    case COMPILER_DRIVER_C_DIALECT_C23:
        return C_PREPROCESS_DIALECT_C23;
    case COMPILER_DRIVER_C_DIALECT_GNU89:
        return C_PREPROCESS_DIALECT_GNU89;
    case COMPILER_DRIVER_C_DIALECT_COUNT:
        return C_PREPROCESS_DIALECT_COUNT;
    }
    return C_PREPROCESS_DIALECT_COUNT;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_argument_error(Arena* arena, CompilerDriverInvocation* invocation, String8 format, String8 argument)
{
    invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
    invocation->diagnostic = string_format(arena, format, argument);
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_cpu_model(Arena* arena, CompilerDriverInvocation* invocation, String8 model_string)
{
    CpuModel model = cpu_model_from_string(model_string);
    if (model == CPU_MODEL_ERROR)
    {
        compiler_driver_argument_error(arena, invocation, S8("unsupported CPU model: {S8}"), model_string);
        return false;
    }
    invocation->target.cpu_model = model;
    invocation->target.cpu_features_explicit = false;
    invocation->target.cpu_features = target_cpu_features_empty();
    return true;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_target(Arena* arena, CompilerDriverInvocation* invocation, String8 target_string)
{
    GpuTargetParseResult gpu = string_equal(target_string, S8("spirv-vulkan1.2-compute"))
                                  ? (GpuTargetParseResult){.error = GPU_TARGET_PARSE_ERROR_NOT_GPU}
                                  : gpu_target_parse(target_string);
    switch (gpu.error)
    {
    case GPU_TARGET_PARSE_ERROR_NONE:
        invocation->gpu_target = gpu.target;
        invocation->has_gpu_target = true;
        return true;
    case GPU_TARGET_PARSE_ERROR_INVALID_TRIPLE:
        compiler_driver_argument_error(arena, invocation, S8("unsupported GPU target component: {S8}"), gpu.invalid_component);
        return false;
    case GPU_TARGET_PARSE_ERROR_SHADER_MODEL:
        compiler_driver_argument_error(arena, invocation, S8("unsupported DXIL shader model: {S8}"), gpu.invalid_component);
        return false;
    case GPU_TARGET_PARSE_ERROR_STAGE:
        compiler_driver_argument_error(arena, invocation, S8("unsupported GPU shader stage: {S8}"), gpu.invalid_component);
        return false;
    case GPU_TARGET_PARSE_ERROR_NOT_GPU:
    case GPU_TARGET_PARSE_ERROR_COUNT:
        break;
    }

    TargetParseResult parsed = target_parse_triple(target_string);
    switch (parsed.error)
    {
    case TARGET_PARSE_ERROR_NONE:
        invocation->target = parsed.target;
        invocation->gpu_target = (GpuTarget){0};
        invocation->has_gpu_target = false;
        return true;
    case TARGET_PARSE_ERROR_CPU_MODEL:
        compiler_driver_argument_error(arena, invocation, S8("CPU model must be selected with -march=: {S8}"), parsed.invalid_component);
        return false;
    case TARGET_PARSE_ERROR_EXCESS_COMPONENT:
        compiler_driver_argument_error(arena, invocation, S8("unsupported target component: {S8}"), parsed.invalid_component);
        return false;
    case TARGET_PARSE_ERROR_ENVIRONMENT:
        compiler_driver_argument_error(arena, invocation,
                                       S8("unsupported target environment: {S8} (MinGW's ABI is not implemented; Windows targets use the MSVC ABI, spelled *-windows-msvc)"),
                                       parsed.invalid_component);
        break;
    case TARGET_PARSE_ERROR_EMPTY:
    case TARGET_PARSE_ERROR_ARCHITECTURE:
    case TARGET_PARSE_ERROR_OPERATING_SYSTEM:
    case TARGET_PARSE_ERROR_COUNT:
        break;
    }
    if (parsed.error != TARGET_PARSE_ERROR_ENVIRONMENT)
    {
        compiler_driver_argument_error(arena, invocation, S8("unsupported target: {S8}"), target_string);
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_gpu_kind_is_spirv(GpuTargetKind kind)
{
    return kind == GPU_TARGET_SPIRV || kind == GPU_TARGET_SPIRV32 || kind == GPU_TARGET_SPIRV64;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_gpu_target_uses_dxc(GpuTargetKind kind)
{
    return kind == GPU_TARGET_DXIL || kind == GPU_TARGET_SPIRV;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_gpu_stage_uses_entry_point(GpuShaderStage stage)
{
    return stage >= GPU_SHADER_STAGE_COMPUTE && stage <= GPU_SHADER_STAGE_AMPLIFICATION;
}

BUSTER_GLOBAL_LOCAL GpuSourceLanguage compiler_driver_gpu_language(CompilerDriverLanguage language)
{
    switch (language)
    {
    case COMPILER_DRIVER_LANGUAGE_AUTOMATIC: return GPU_SOURCE_LANGUAGE_AUTOMATIC;
    case COMPILER_DRIVER_LANGUAGE_OPENCL: return GPU_SOURCE_LANGUAGE_OPENCL;
    case COMPILER_DRIVER_LANGUAGE_CUDA: return GPU_SOURCE_LANGUAGE_CUDA;
    case COMPILER_DRIVER_LANGUAGE_HIP: return GPU_SOURCE_LANGUAGE_HIP;
    case COMPILER_DRIVER_LANGUAGE_METAL: return GPU_SOURCE_LANGUAGE_METAL;
    case COMPILER_DRIVER_LANGUAGE_HLSL: return GPU_SOURCE_LANGUAGE_HLSL;
    case COMPILER_DRIVER_LANGUAGE_LLVM_IR: return GPU_SOURCE_LANGUAGE_LLVM_IR;
    case COMPILER_DRIVER_LANGUAGE_SPIRV_BINARY: return GPU_SOURCE_LANGUAGE_SPIRV_BINARY;
    case COMPILER_DRIVER_LANGUAGE_METAL_AIR: return GPU_SOURCE_LANGUAGE_METAL_AIR;
    case COMPILER_DRIVER_LANGUAGE_C:
    case COMPILER_DRIVER_LANGUAGE_ASSEMBLY:
    case COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT:
    case COMPILER_DRIVER_LANGUAGE_COUNT: break;
    }
    return GPU_SOURCE_LANGUAGE_COUNT;
}

// Keep the native-language boundary independent of enum ordering. Assembly is
// a native input even though its enum value follows the GPU source languages;
// invalid values are not native and are rejected by the native resolver.
bool compiler_driver_language_is_native(CompilerDriverLanguage language)
{
    bool result;
    switch (language)
    {
    case COMPILER_DRIVER_LANGUAGE_AUTOMATIC:
    case COMPILER_DRIVER_LANGUAGE_C:
    case COMPILER_DRIVER_LANGUAGE_ASSEMBLY:
    case COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT:
        result = true;
        break;
    case COMPILER_DRIVER_LANGUAGE_OPENCL:
    case COMPILER_DRIVER_LANGUAGE_CUDA:
    case COMPILER_DRIVER_LANGUAGE_HIP:
    case COMPILER_DRIVER_LANGUAGE_METAL:
    case COMPILER_DRIVER_LANGUAGE_HLSL:
    case COMPILER_DRIVER_LANGUAGE_LLVM_IR:
    case COMPILER_DRIVER_LANGUAGE_SPIRV_BINARY:
    case COMPILER_DRIVER_LANGUAGE_METAL_AIR:
    case COMPILER_DRIVER_LANGUAGE_COUNT:
    default:
        result = false;
        break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverLanguage compiler_driver_input_language(CompilerDriverInvocation invocation, u32 input_index)
{
    CompilerDriverLanguage result = invocation.language;
    if (invocation.input_languages && input_index < invocation.input_language_count)
    {
        result = invocation.input_languages[input_index];
    }
    return result;
}

// Parsed streams cover both indexed arrays in their original order. A legacy
// API invocation omits the stream and retains its file-then-library placement.
BUSTER_GLOBAL_LOCAL bool compiler_driver_link_operations_valid(CompilerDriverInvocation const* invocation)
{
    bool result = !invocation->link_operation_count ||
                  (invocation->link_operations && (u64)invocation->link_operation_count == (u64)invocation->input_count + invocation->library_count);
    u32 inputs = 0;
    u32 libraries = 0;
    for (u32 index = 0; result && index < invocation->link_operation_count; index += 1)
    {
        CompilerDriverLinkOperation operation = invocation->link_operations[index];
        switch (operation.kind)
        {
        case COMPILER_DRIVER_LINK_OPERATION_FILE:
            result = inputs < invocation->input_count && operation.index == inputs;
            inputs += result;
            break;
        case COMPILER_DRIVER_LINK_OPERATION_LIBRARY:
            result = libraries < invocation->library_count && operation.index == libraries;
            libraries += result;
            break;
        case COMPILER_DRIVER_LINK_OPERATION_COUNT:
        default:
            result = false;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverLinkOperation compiler_driver_link_operation(CompilerDriverInvocation const* invocation, u64 index)
{
    CompilerDriverLinkOperation result;
    if (invocation->action == COMPILER_DRIVER_ACTION_LINK && invocation->link_operation_count)
    {
        result = invocation->link_operations[index];
    }
    else if (index < invocation->input_count)
    {
        result = (CompilerDriverLinkOperation){.index = (u32)index, .kind = COMPILER_DRIVER_LINK_OPERATION_FILE};
    }
    else
    {
        result = (CompilerDriverLinkOperation){.index = (u32)(index - invocation->input_count), .kind = COMPILER_DRIVER_LINK_OPERATION_LIBRARY};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_invocation_has_language(CompilerDriverInvocation invocation, CompilerDriverLanguage language)
{
    bool result = invocation.language == language;
    if (invocation.input_languages)
    {
        result = false;
        for (u32 input_index = 0; input_index < invocation.input_count && !result; input_index += 1)
        {
            result = compiler_driver_input_language(invocation, input_index) == language;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_invocation_languages_are_native(CompilerDriverInvocation invocation)
{
    bool result = compiler_driver_language_is_native(invocation.language);
    if (invocation.input_languages)
    {
        result = true;
        for (u32 input_index = 0; input_index < invocation.input_count && result; input_index += 1)
        {
            result = compiler_driver_language_is_native(compiler_driver_input_language(invocation, input_index));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL GpuSourceLanguage compiler_driver_gpu_effective_language(CompilerDriverInvocation invocation, u32 input_index)
{
    CompilerDriverLanguage selected = compiler_driver_input_language(invocation, input_index);
    GpuSourceLanguage language = compiler_driver_gpu_language(selected);
    String8 path = invocation.input_paths[input_index];
    return language == GPU_SOURCE_LANGUAGE_AUTOMATIC ? gpu_source_language_from_path(path) : language;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_metal_sdk_is_valid(String8 sdk)
{
    String8 values[] = {
        S8("macosx"), S8("iphoneos"), S8("iphonesimulator"), S8("appletvos"),
        S8("appletvsimulator"), S8("xros"), S8("xrsimulator"),
    };
    bool result = false;
    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values) && !result; value_index += 1)
    {
        result = string_equal(sdk, values[value_index]);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_dxc_shader_model_is_valid(GpuTarget target)
{
    bool result;
    if (target.shader_model_major != 6 || target.shader_model_minor > 10)
    {
        result = false;
    }
    else if ((target.stage == GPU_SHADER_STAGE_MESH || target.stage == GPU_SHADER_STAGE_AMPLIFICATION) && target.shader_model_minor < 5)
    {
        result = false;
    }
    else if (target.stage >= GPU_SHADER_STAGE_LIBRARY && target.shader_model_minor == 0)
    {
        result = false;
    }
    else
    {
        result = true;
    }

    return result;
}

typedef struct CompilerDriverFeatureOverride CompilerDriverFeatureOverride;
struct CompilerDriverFeatureOverride
{
    String8 name;
    bool enable;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_parse_feature_overrides(Arena* arena, CompilerDriverInvocation* invocation, String8 value,
                                                                  CompilerDriverFeatureOverride* overrides, u64 override_capacity, u64* override_count)
{
    u64 start = 0;
    while (start < value.length)
    {
        u64 end = start;
        while (end < value.length && value.pointer[end] != ',')
        {
            end += 1;
        }
        String8 item = string_slice(value, start, end);
        if (item.length < 2 || (item.pointer[0] != '+' && item.pointer[0] != '-'))
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), item);
            return false;
        }
        if (*override_count >= override_capacity)
        {
            compiler_driver_argument_error(arena, invocation, S8("too many target feature overrides: {S8}"), value);
            return false;
        }
        overrides[*override_count] = (CompilerDriverFeatureOverride){
            .name = string_slice(item, 1, item.length),
            .enable = item.pointer[0] == '+',
        };
        *override_count += 1;
        if (end == value.length)
        {
            return true;
        }
        start = end + 1;
        if (start == value.length)
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), value);
            return false;
        }
    }
    compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), value);
    return false;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_assembly_syntax(Arena* arena, CompilerDriverInvocation* invocation, String8 value)
{
    if (string_equal(value, S8("att")))
    {
        invocation->assembly_syntax = ASSEMBLY_SYNTAX_ATT;
        return true;
    }
    if (string_equal(value, S8("intel")))
    {
        invocation->assembly_syntax = ASSEMBLY_SYNTAX_INTEL;
        return true;
    }
    compiler_driver_argument_error(arena, invocation, S8("unsupported assembly syntax: {S8}"), value);
    return false;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_entry_symbol(Target target)
{
    return target.os == OPERATING_SYSTEM_UEFI ? S8("UefiMain") : S8("main");
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_executable_path(Target target)
{
    return target.os == OPERATING_SYSTEM_UEFI      ? S8("a.efi")
           : target.os == OPERATING_SYSTEM_WINDOWS ? S8("a.exe")
                                                   : S8("a.out");
}

// A GPU pipeline runs entirely in an external toolchain, so every option that
// only the native backend understands is a mistake rather than a no-op.
BUSTER_GLOBAL_LOCAL void compiler_driver_reject_gpu_native_options(Arena* arena, CompilerDriverInvocation* invocation, u64 feature_override_count)
{
    GpuTargetKind gpu_kind = invocation->gpu_target.kind;
    bool spirv_target = compiler_driver_gpu_kind_is_spirv(gpu_kind);
    bool dxc_target = compiler_driver_gpu_target_uses_dxc(gpu_kind);
    bool llvm_gpu_target = spirv_target || gpu_kind == GPU_TARGET_NVPTX32 || gpu_kind == GPU_TARGET_NVPTX64 || gpu_kind == GPU_TARGET_AMDGCN;
    if (invocation->emit_llvm_bitcode)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("-emit-llvm is not supported for external GPU target pipelines");
    }
    else if (feature_override_count)
    {
        compiler_driver_argument_error(arena, invocation, S8("-mattr is not supported for GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->assembly_syntax != ASSEMBLY_SYNTAX_DEFAULT)
    {
        compiler_driver_argument_error(arena, invocation, S8("assembly syntax is not supported for GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->register_allocator_explicit)
    {
        compiler_driver_argument_error(arena, invocation, S8("the native register allocator is not used by GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->c_dialect_explicit)
    {
        compiler_driver_argument_error(arena, invocation, S8("the native C dialect option is not used by GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->plain_char_policy_explicit)
    {
        compiler_driver_argument_error(arena, invocation, S8("plain-char signedness options are not supported for external GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->source_metrics_path.length)
    {
        compiler_driver_argument_error(arena, invocation, S8("source metrics are not supported for GPU target: {S8}"), invocation->source_metrics_path);
    }
    else if (invocation->collect_input_metrics || invocation->keep_going)
    {
        compiler_driver_argument_error(arena, invocation, S8("per-input metrics and -fkeep-going are not supported for GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (compiler_driver_invocation_has_language(*invocation, COMPILER_DRIVER_LANGUAGE_C) ||
             compiler_driver_invocation_has_language(*invocation, COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT))
    {
        CompilerDriverLanguage language = compiler_driver_invocation_has_language(*invocation, COMPILER_DRIVER_LANGUAGE_C)
                                              ? COMPILER_DRIVER_LANGUAGE_C
                                              : COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT;
        compiler_driver_argument_error(arena, invocation, S8("native source language is incompatible with GPU target: {S8}"),
                                       language == COMPILER_DRIVER_LANGUAGE_C ? S8("c") : S8("cpp-output"));
    }
    else if (invocation->library_path_count || invocation->library_count || invocation->framework_path_count || invocation->framework_count ||
        invocation->linker_argument_count)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("native libraries, frameworks, and linker arguments cannot be used in a GPU pipeline; pass backend options with -Xgpu");
    }
    else if (invocation->action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY && invocation->output_path.length)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("cannot specify -o with -fsyntax-only for a GPU target");
    }
    else if (invocation->cuda_path.length && gpu_kind != GPU_TARGET_NVPTX32 && gpu_kind != GPU_TARGET_NVPTX64)
    {
        compiler_driver_argument_error(arena, invocation, S8("CUDA toolkit path is incompatible with GPU target: {S8}"), invocation->cuda_path);
    }
    else if (invocation->rocm_path.length && gpu_kind != GPU_TARGET_AMDGCN)
    {
        compiler_driver_argument_error(arena, invocation, S8("ROCm path is incompatible with GPU target: {S8}"), invocation->rocm_path);
    }
    else if ((invocation->gpu_tools.spirv_link_path.length || invocation->gpu_tools.spirv_dis_path.length) && !spirv_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("SPIR-V tool override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.xcrun_path.length && gpu_kind != GPU_TARGET_METAL_AIR64)
    {
        compiler_driver_argument_error(arena, invocation, S8("xcrun override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.dxc_path.length && !dxc_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("DXC override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.clang_path.length && !llvm_gpu_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("GPU Clang override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.llc_path.length && !llvm_gpu_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("GPU llc override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
}

// Native code-generation policies need the native code generator, so the
// pipeline a resolved invocation selects must be able to honor each one.
// argv_request enables the rules over state only argv fills in:
// metrics_output_path belongs to `ide cc`, never to an API request.
BUSTER_GLOBAL_LOCAL void compiler_driver_validate_codegen_request(CompilerDriverInvocation* invocation, bool argv_request)
{
    bool native_codegen_action = invocation->action != COMPILER_DRIVER_ACTION_PREPROCESS && invocation->action != COMPILER_DRIVER_ACTION_SYNTAX_ONLY;
    bool native_machine = !invocation->has_gpu_target && !invocation->emit_llvm_bitcode &&
                          (invocation->target.cpu_arch == CPU_ARCH_X86_64 || invocation->target.cpu_arch == CPU_ARCH_AARCH64);
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (invocation->emit_llvm_bitcode &&
            (invocation->action == COMPILER_DRIVER_ACTION_PREPROCESS || invocation->action == COMPILER_DRIVER_ACTION_ASSEMBLY ||
             invocation->action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY))
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("-emit-llvm emits binary bitcode and cannot be combined with -E, -S, or -fsyntax-only");
        }
        else if (invocation->verify_codegen && !(native_machine && native_codegen_action))
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("-fverify-codegen requires native x86-64 or AArch64 code generation");
        }
        else if (invocation->sysv_bitfield_abi_explicit &&
                 (invocation->has_gpu_target || invocation->emit_llvm_bitcode || invocation->target.cpu_arch != CPU_ARCH_X86_64 ||
                  ir_abi_convention_for_target(invocation->target) != IR_ABI_CONVENTION_SYSTEMV_X86_64))
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("-fsysv-unnamed-bitfields requires native System V x86-64 code generation");
        }
        else if (argv_request && invocation->collect_function_sizes && !invocation->metrics_output_path.length)
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("-fmetrics-functions requires -fmetrics-out=FILE");
        }
        else if (invocation->record_codegen_fallbacks && !(native_machine && native_codegen_action))
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("-fcodegen-fallback-census requires native x86-64 or AArch64 code generation");
        }
        else if (invocation->reject_machine_fallback && !(native_machine && native_codegen_action))
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("-fno-machine-fallback requires native x86-64 or AArch64 code generation");
        }
    }
}

// The request-combination authority both public entry points share.
// compiler_driver_parse_arguments runs each part at its own point in
// finalization, the GPU part before the GPU target resolves, so argv
// diagnostics keep their order. compiler_driver_execute_invocation runs it
// whole on caller-built requests before any input is mapped, a temporary
// directory is created or a tool is spawned. -mattr overrides and the
// -fmetrics-out path exist only as argv state and stay parser-only;
// zero-valued API fields mean "not requested", never a CLI default.
BUSTER_GLOBAL_LOCAL void compiler_driver_validate_request(Arena* arena, CompilerDriverInvocation* invocation)
{
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->has_gpu_target)
    {
        compiler_driver_reject_gpu_native_options(arena, invocation, 0);
    }
    compiler_driver_validate_codegen_request(invocation, false);
}

// Folds the GPU architecture, shader stage, shader model, entry point and Metal
// SDK the command line asked for into the target itself. Each step runs only
// while the ones before it agreed.
BUSTER_GLOBAL_LOCAL void compiler_driver_resolve_gpu_target(Arena* arena, CompilerDriverInvocation* invocation, String8 architecture_option)
{
    GpuTargetKind gpu_kind = invocation->gpu_target.kind;
    bool dxc_target = compiler_driver_gpu_target_uses_dxc(gpu_kind);
    if (invocation->gpu_architecture.length && architecture_option.length && !string_equal(invocation->gpu_architecture, architecture_option))
    {
        compiler_driver_argument_error(arena, invocation, S8("conflicting GPU architectures: {S8}"), invocation->gpu_architecture);
    }
    String8 gpu_architecture = invocation->gpu_architecture.length ? invocation->gpu_architecture : architecture_option;
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && gpu_architecture.length)
    {
        if (gpu_kind != GPU_TARGET_NVPTX32 && gpu_kind != GPU_TARGET_NVPTX64 && gpu_kind != GPU_TARGET_AMDGCN)
        {
            compiler_driver_argument_error(arena, invocation, S8("GPU architecture is incompatible with target: {S8}"), gpu_architecture);
        }
        else
        {
            invocation->gpu_target.architecture = gpu_architecture;
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->gpu_stage.length)
    {
        GpuShaderStage stage = gpu_shader_stage_from_string(invocation->gpu_stage);
        if (stage == GPU_SHADER_STAGE_NONE)
        {
            compiler_driver_argument_error(arena, invocation, S8("unsupported GPU shader stage: {S8}"), invocation->gpu_stage);
        }
        else if (gpu_kind == GPU_TARGET_METAL_AIR64 || (!dxc_target && stage != GPU_SHADER_STAGE_COMPUTE))
        {
            compiler_driver_argument_error(arena, invocation, S8("shader stage is incompatible with GPU target: {S8}"), invocation->gpu_stage);
        }
        else
        {
            invocation->gpu_target.stage = stage;
            if (dxc_target && !compiler_driver_gpu_stage_uses_entry_point(stage))
            {
                invocation->gpu_target.entry_point = (String8){0};
            }
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->gpu_shader_model.length &&
        (!dxc_target || !gpu_shader_model_parse(invocation->gpu_shader_model, &invocation->gpu_target.shader_model_major,
                                                &invocation->gpu_target.shader_model_minor)))
    {
        compiler_driver_argument_error(arena, invocation, S8("shader model is incompatible with GPU target: {S8}"), invocation->gpu_shader_model);
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && dxc_target && !compiler_driver_dxc_shader_model_is_valid(invocation->gpu_target))
    {
        compiler_driver_argument_error(arena, invocation, S8("unsupported shader model for target stage: {S8}"),
                                       invocation->gpu_shader_model.length ? invocation->gpu_shader_model
                                                                           : gpu_target_to_string(arena, invocation->gpu_target));
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (invocation->gpu_entry_point.length)
        {
            if (!dxc_target || !compiler_driver_gpu_stage_uses_entry_point(invocation->gpu_target.stage))
            {
                compiler_driver_argument_error(arena, invocation, S8("GPU entry point is incompatible with target or stage: {S8}"),
                                               invocation->gpu_entry_point);
            }
            else
            {
                invocation->gpu_target.entry_point = invocation->gpu_entry_point;
            }
        }
        else if (dxc_target && compiler_driver_gpu_stage_uses_entry_point(invocation->gpu_target.stage) && !invocation->gpu_target.entry_point.length)
        {
            invocation->gpu_target.entry_point = S8("main");
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->metal_sdk.length)
    {
        if (gpu_kind != GPU_TARGET_METAL_AIR64 || !compiler_driver_metal_sdk_is_valid(invocation->metal_sdk))
        {
            compiler_driver_argument_error(arena, invocation, S8("unsupported Metal SDK for GPU target: {S8}"), invocation->metal_sdk);
        }
        else
        {
            invocation->gpu_target.metal_sdk = invocation->metal_sdk;
        }
    }
}

// The options a GPU pipeline accepts depend on what the inputs actually are:
// HLSL sources reach DXC, CUDA sources need the toolkit path.
BUSTER_GLOBAL_LOCAL void compiler_driver_check_gpu_inputs(Arena* arena, CompilerDriverInvocation* invocation)
{
    GpuTargetKind gpu_kind = invocation->gpu_target.kind;
    bool has_hlsl_input = gpu_kind == GPU_TARGET_DXIL;
    bool has_cuda_input = false;
    for (u32 input_index = 0; input_index < invocation->input_count; input_index += 1)
    {
        GpuSourceLanguage input_language = compiler_driver_gpu_effective_language(*invocation, input_index);
        has_hlsl_input = has_hlsl_input || input_language == GPU_SOURCE_LANGUAGE_HLSL;
        has_cuda_input = has_cuda_input || input_language == GPU_SOURCE_LANGUAGE_CUDA;
    }
    if (gpu_kind == GPU_TARGET_SPIRV && !has_hlsl_input &&
        (invocation->gpu_entry_point.length || invocation->gpu_shader_model.length || invocation->gpu_tools.dxc_path.length ||
         invocation->gpu_target.stage != GPU_SHADER_STAGE_COMPUTE))
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("SPIR-V shader stage, entry point, shader model, and DXC options require HLSL input");
    }
    else if (invocation->cuda_path.length && !has_cuda_input)
    {
        compiler_driver_argument_error(arena, invocation, S8("CUDA toolkit path requires CUDA input: {S8}"), invocation->cuda_path);
    }
    else if (has_hlsl_input && (invocation->sysroot.length || invocation->no_standard_includes))
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("DXC HLSL pipelines do not support -isysroot/--sysroot or -nostdinc");
    }
    else if (!gpu_target_is_valid(invocation->gpu_target))
    {
        String8 target = gpu_target_to_string(arena, invocation->gpu_target);
        compiler_driver_argument_error(arena, invocation, S8("incomplete GPU target configuration: {S8}"), target);
    }
}

// The x86-64 psABI microarchitecture levels, spelled -march=x86-64 and
// -march=x86-64-v2..v4. Level 1 is Buster's `baseline` model; each higher
// level is that model plus the cumulative features below. The psABI's LAHF-SAHF
// has no target feature: long mode on every supported x86-64 host has it.
typedef struct CompilerDriverPsabiFeature CompilerDriverPsabiFeature;
struct CompilerDriverPsabiFeature
{
    String8 name;
    u8 level;
};

BUSTER_GLOBAL_LOCAL CompilerDriverPsabiFeature const compiler_driver_psabi_features[] = {
    {S8_INITIALIZER("cx16"), 2}, {S8_INITIALIZER("popcnt"), 2}, {S8_INITIALIZER("sse3"), 2}, {S8_INITIALIZER("ssse3"), 2},
    {S8_INITIALIZER("sse4.1"), 2}, {S8_INITIALIZER("sse4.2"), 2},
    {S8_INITIALIZER("avx"), 3}, {S8_INITIALIZER("avx2"), 3}, {S8_INITIALIZER("bmi1"), 3}, {S8_INITIALIZER("bmi2"), 3},
    {S8_INITIALIZER("f16c"), 3}, {S8_INITIALIZER("fma"), 3}, {S8_INITIALIZER("lzcnt"), 3}, {S8_INITIALIZER("movbe"), 3},
    {S8_INITIALIZER("xsave"), 3},
    {S8_INITIALIZER("avx512f"), 4}, {S8_INITIALIZER("avx512bw"), 4}, {S8_INITIALIZER("avx512cd"), 4}, {S8_INITIALIZER("avx512dq"), 4},
    {S8_INITIALIZER("avx512vl"), 4},
};

// 0 when the spelling is not a psABI level.
BUSTER_GLOBAL_LOCAL u32 compiler_driver_psabi_level(String8 name)
{
    u32 result = 0;
    if (string_equal(name, S8("x86-64")))
    {
        result = 1;
    }
    else if (string_equal(name, S8("x86-64-v2")))
    {
        result = 2;
    }
    else if (string_equal(name, S8("x86-64-v3")))
    {
        result = 3;
    }
    else if (string_equal(name, S8("x86-64-v4")))
    {
        result = 4;
    }
    return result;
}

// Resolves the native target's CPU model and feature set, and rejects the GPU
// options that have no target to apply to.
BUSTER_GLOBAL_LOCAL void compiler_driver_resolve_native_target(Arena* arena, CompilerDriverInvocation* invocation, String8 architecture_option,
                                                               CompilerDriverFeatureOverride* feature_overrides, u64 feature_override_count)
{
    u32 psabi_level = 0;
    bool gpu_option = invocation->gpu_architecture.length || invocation->gpu_entry_point.length || invocation->gpu_stage.length ||
                      invocation->gpu_shader_model.length || invocation->metal_sdk.length || invocation->cuda_path.length || invocation->rocm_path.length ||
                      invocation->gpu_tools.clang_path.length || invocation->gpu_tools.llc_path.length || invocation->gpu_tools.spirv_link_path.length ||
                      invocation->gpu_tools.spirv_dis_path.length || invocation->gpu_tools.xcrun_path.length || invocation->gpu_tools.dxc_path.length ||
                      invocation->gpu_argument_count || invocation->save_gpu_temporaries || !compiler_driver_invocation_languages_are_native(*invocation);
    if (gpu_option)
    {
        compiler_driver_argument_error(arena, invocation, S8("GPU option requires a GPU target: {S8}"),
                                       S8("use --target=spirv64, nvptx64-nvidia-cuda, amdgcn-amd-amdhsa, air64-apple-macos, or dxil"));
    }
    else if (architecture_option.length)
    {
        // The psABI levels exist only for x86-64; elsewhere the spelling is an
        // unknown CPU model, as it always was.
        psabi_level = invocation->target.cpu_arch == CPU_ARCH_X86_64 ? compiler_driver_psabi_level(architecture_option) : 0;
        compiler_driver_set_cpu_model(arena, invocation, psabi_level ? S8("baseline") : architecture_option);
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && (feature_override_count || psabi_level > 1))
    {
        invocation->target.cpu_features = target_cpu_features_effective(invocation->target);
        invocation->target.cpu_features_explicit = true;
        // The level's features come first so that -mattr overrides, wherever
        // they sit on the command line, keep refining the selected level.
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(compiler_driver_psabi_features); index += 1)
        {
            if (compiler_driver_psabi_features[index].level <= psabi_level)
            {
                invocation->target.cpu_features = target_cpu_features_add(
                    invocation->target.cpu_features, target_cpu_feature_from_string(CPU_ARCH_X86_64, compiler_driver_psabi_features[index].name));
            }
        }
        for (u64 override_index = 0; override_index < feature_override_count && invocation->error == COMPILER_DRIVER_ERROR_NONE; override_index += 1)
        {
            CompilerDriverFeatureOverride override = feature_overrides[override_index];
            TargetCpuFeature feature = target_cpu_feature_from_string(invocation->target.cpu_arch, override.name);
            if (feature == TARGET_CPU_FEATURE_NONE)
            {
                compiler_driver_argument_error(arena, invocation, S8("unsupported target feature: {S8}"), override.name);
            }
            else if (override.enable)
            {
                invocation->target.cpu_features = target_cpu_features_add(invocation->target.cpu_features, feature);
            }
            else
            {
                invocation->target.cpu_features = target_cpu_features_remove(invocation->target.cpu_features, feature);
            }
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && !target_cpu_features_are_valid(invocation->target))
    {
        if (feature_override_count || psabi_level > 1)
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature combination: {S8}"),
                                           target_cpu_features_to_string(arena, invocation->target));
        }
        else
        {
            compiler_driver_argument_error(arena, invocation, S8("CPU model is incompatible with target: {S8}"),
                                           cpu_model_to_string_os(invocation->target.cpu_model));
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->target.cpu_arch != CPU_ARCH_X86_64 &&
        invocation->assembly_syntax != ASSEMBLY_SYNTAX_DEFAULT)
    {
        compiler_driver_argument_error(arena, invocation, S8("assembly syntax is incompatible with target: {S8}"),
                                       invocation->assembly_syntax == ASSEMBLY_SYNTAX_ATT ? S8("att") : S8("intel"));
    }
}

// Shared by argv and invocation-API entry points; reject the direct compute
// contract before source mapping, process spawning, or output publication.
BUSTER_GLOBAL_LOCAL bool compiler_driver_c_input(CompilerDriverLanguage language, String8 path);
BUSTER_GLOBAL_LOCAL bool compiler_driver_object_input(String8 path);
BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_input(String8 path);
BUSTER_GLOBAL_LOCAL bool compiler_driver_preprocessed_assembly_input(String8 path);
BUSTER_GLOBAL_LOCAL void compiler_driver_validate_native_pic_invocation(Arena* arena, CompilerDriverInvocation* invocation, String8 option);
BUSTER_GLOBAL_LOCAL void compiler_driver_validate_spirv_invocation(CompilerDriverInvocation* invocation)
{
    if (invocation->target.cpu_arch == CPU_ARCH_SPIRV_COMPUTE && invocation->error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (invocation->target.os != OPERATING_SYSTEM_FREESTANDING || !target_cpu_features_are_valid(invocation->target) ||
            invocation->has_gpu_target || invocation->emit_llvm_bitcode || invocation->action != COMPILER_DRIVER_ACTION_OBJECT ||
            invocation->input_count != 1 || !invocation->input_paths ||
            (invocation->input_languages && invocation->input_language_count != invocation->input_count))
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("direct SPIR-V compute requires spirv-vulkan1.2-compute, one C input, and -c binary output");
        }
        else if (!compiler_driver_c_input(compiler_driver_input_language(*invocation, 0), invocation->input_paths[0]) ||
                 compiler_driver_object_input(invocation->input_paths[0]) || compiler_driver_archive_input(invocation->input_paths[0]) ||
                 compiler_driver_preprocessed_assembly_input(invocation->input_paths[0]))
        {
            invocation->error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            invocation->diagnostic = S8("direct SPIR-V compute accepts only C source");
        }
        else if (invocation->library_count || invocation->library_path_count || invocation->framework_count ||
                 invocation->framework_path_count || invocation->linker_argument_count || invocation->gpu_argument_count ||
                 invocation->gpu_architecture.length || invocation->gpu_entry_point.length || invocation->gpu_stage.length ||
                 invocation->gpu_shader_model.length || invocation->metal_sdk.length || invocation->cuda_path.length ||
                 invocation->rocm_path.length || invocation->gpu_tools.clang_path.length || invocation->gpu_tools.llc_path.length ||
                 invocation->gpu_tools.spirv_link_path.length || invocation->gpu_tools.spirv_dis_path.length ||
                 invocation->gpu_tools.xcrun_path.length || invocation->gpu_tools.dxc_path.length || invocation->save_gpu_temporaries ||
                 invocation->verify_codegen || invocation->record_codegen_fallbacks || invocation->reject_machine_fallback ||
                 invocation->bootstrap_trace_prefix.length || invocation->sysv_bitfield_abi_explicit ||
                 invocation->image_kind != NATIVE_IMAGE_EXECUTABLE || invocation->register_allocator_explicit ||
                 (invocation->debug_info_explicit && invocation->debug_info) || invocation->position_independent)
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = S8("native linking, allocation, debug/PIC, verification, and external GPU options are unsupported for direct SPIR-V compute");
        }
    }
}

// The builtin resource headers plus whatever the sysroot, the target triple, or
// the host environment says the system headers are.
#if BUSTER_WINDOWS || BUSTER_INCLUDE_TESTS
// Reserve from the actual INCLUDE population, not from a fixed allowance.
// The prefix already contains -isystem and resource paths in search order.
BUSTER_GLOBAL_LOCAL void compiler_driver_append_environment_includes(Arena* arena, CompilerDriverInvocation* invocation, String8 includes)
{
    u64 extra_count = 0;
    for (u64 i = 0; i < includes.length; ++i)
        extra_count += includes.pointer[i] != ';' && (i == 0 || includes.pointer[i - 1] == ';');
    u64 count = (u64)invocation->system_include_path_count + extra_count;
    if (count > UINT32_MAX)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("too many system include paths");
    }
    else if (extra_count)
    {
        String8* paths = arena_allocate(arena, String8, count);
        if (invocation->system_include_path_count)
            memcpy(paths, invocation->system_include_paths, (u64)invocation->system_include_path_count * sizeof(*paths));
        // Keep the environment snapshot alive for the invocation, independently
        // of later environment queries or the caller's temporary storage.
        includes = string_duplicate_arena(arena, includes, false);
        for (u64 start = 0; start < includes.length;)
        {
            u64 end = start;
            while (end < includes.length && includes.pointer[end] != ';')
                ++end;
            if (end != start)
                paths[invocation->system_include_path_count++] = string_slice(includes, start, end);
            start = end + (end < includes.length);
        }
        invocation->system_include_paths = paths;
    }
}
#endif

#if BUSTER_INCLUDE_TESTS
void compiler_driver_test_append_environment_includes(Arena* arena, CompilerDriverInvocation* invocation, String8 includes)
{
    compiler_driver_append_environment_includes(arena, invocation, includes);
}
#endif

BUSTER_GLOBAL_LOCAL void compiler_driver_append_system_includes(Arena* arena, CompilerDriverInvocation* invocation)
{
#if defined(BUSTER_HOST_C_RESOURCE_INCLUDE)
    if (sizeof(BUSTER_HOST_C_RESOURCE_INCLUDE) > 1)
    {
        invocation->system_include_paths[invocation->system_include_path_count++] = S8(BUSTER_HOST_C_RESOURCE_INCLUDE);
    }
#endif
    if (invocation->sysroot.length)
    {
        if (invocation->target.os == OPERATING_SYSTEM_WASI)
        {
            invocation->system_include_paths[invocation->system_include_path_count++] =
                string_format(arena, S8("{S8}/include/wasm32-wasip1"), invocation->sysroot);
            invocation->system_include_paths[invocation->system_include_path_count++] =
                string_format(arena, S8("{S8}/include/wasm32-wasi"), invocation->sysroot);
            invocation->system_include_paths[invocation->system_include_path_count++] = string_format(arena, S8("{S8}/include"), invocation->sysroot);
        }
        else
        {
            invocation->system_include_paths[invocation->system_include_path_count++] = string_format(arena, S8("{S8}/usr/local/include"), invocation->sysroot);
            if (invocation->target.os == OPERATING_SYSTEM_LINUX || invocation->target.os == OPERATING_SYSTEM_ANDROID)
            {
                String8 multiarch = invocation->target.cpu_arch == CPU_ARCH_AARCH64
                                        ? (invocation->target.os == OPERATING_SYSTEM_ANDROID ? S8("aarch64-linux-android") : S8("aarch64-linux-gnu"))
                                        : (invocation->target.os == OPERATING_SYSTEM_ANDROID ? S8("x86_64-linux-android") : S8("x86_64-linux-gnu"));
                invocation->system_include_paths[invocation->system_include_path_count++] =
                    string_format(arena, S8("{S8}/usr/include/{S8}"), invocation->sysroot, multiarch);
            }
            else if (invocation->target.os == OPERATING_SYSTEM_WINDOWS)
            {
                invocation->system_include_paths[invocation->system_include_path_count++] =
                    string_format(arena, S8("{S8}/x86_64-w64-mingw32/include"), invocation->sysroot);
                invocation->system_include_paths[invocation->system_include_path_count++] = string_format(arena, S8("{S8}/include"), invocation->sysroot);
            }
            invocation->system_include_paths[invocation->system_include_path_count++] = string_format(arena, S8("{S8}/usr/include"), invocation->sysroot);
        }
    }
    else if (invocation->target.cpu_arch == target_native.cpu_arch && invocation->target.os == target_native.os)
    {
#if BUSTER_LINUX
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/local/include");
#if BUSTER_CPU_ARCH_X86_64
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/include/x86_64-linux-gnu");
#else
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/include/aarch64-linux-gnu");
#endif
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/include");
#endif
#if BUSTER_WINDOWS
        // Capture once: counting and appending must see the same snapshot.
        compiler_driver_append_environment_includes(arena, invocation, os_get_environment_variable(S8("INCLUDE")));
#endif
    }
}

// Response files. Before any option is read, an argument that begins with '@'
// is replaced by the arguments held in the file it names, as GCC's expandargv
// and Clang's GNU tokenizer do, including after `--`. Whitespace separates
// arguments; single and double quotes group bytes and are dropped, so ""
// is an empty argument and a"b c"d is one; a backslash takes the next byte
// literally inside or outside quotes. Text those compilers accept in
// divergent ways is refused here instead: an unterminated quote, a trailing
// backslash, a NUL byte and a bare "@". An expanded argument that itself
// begins with '@' is refused rather than expanded: nesting is not supported.
// Each expanded argument is NUL-terminated like an argv entry, and all of
// them live in the invocation arena. Bounds are in driver.h.
typedef enum CompilerDriverResponseFileStatus
{
    COMPILER_DRIVER_RESPONSE_FILE_READ,
    COMPILER_DRIVER_RESPONSE_FILE_UNREADABLE,
    COMPILER_DRIVER_RESPONSE_FILE_OVERSIZED,
} CompilerDriverResponseFileStatus;

typedef struct CompilerDriverResponseFileSplit CompilerDriverResponseFileSplit;
struct CompilerDriverResponseFileSplit
{
    // A diagnostic format taking the response-file path; empty on success.
    String8 failure;
    u64 argument_count;
    // Unescaped argument bytes plus one terminator per argument.
    u64 byte_count;
};

// Requests one byte past `limit`, so an oversized file, pipe or device is
// detected without trusting a reported size or reading it to its end.
BUSTER_GLOBAL_LOCAL CompilerDriverResponseFileStatus compiler_driver_response_file_read(Arena* arena, String8 path, u64 limit, String8* content)
{
    CompilerDriverResponseFileStatus status = COMPILER_DRIVER_RESPONSE_FILE_UNREADABLE;
    OsFileOpenResult opened = os_file_open_checked(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
    if (opened.file)
    {
        u8* buffer = (u8*)arena_allocate_bytes(arena, limit + 1, 1);
        OsFileReadResult read = os_file_read_exact(opened.file, (ByteSlice){.pointer = buffer, .length = limit + 1});
        OsError close_error = os_file_close_checked(opened.file);
        u64 kept = 0;
        if (read.status != OS_FILE_READ_ERROR && !close_error.v)
        {
            if (read.transferred > limit)
            {
                status = COMPILER_DRIVER_RESPONSE_FILE_OVERSIZED;
            }
            else
            {
                status = COMPILER_DRIVER_RESPONSE_FILE_READ;
                kept = read.transferred;
                *content = (String8){.pointer = (char8*)buffer, .length = kept};
            }
        }
        arena_set_position(arena, (u64)(buffer - (u8*)arena) + kept);
    }
    return status;
}

// With null `arguments` this only validates and counts. Otherwise it writes
// the arguments into `bytes`, which holds the counted byte_count.
BUSTER_GLOBAL_LOCAL CompilerDriverResponseFileSplit compiler_driver_response_file_split(String8 content, String8* arguments, char8* bytes)
{
    CompilerDriverResponseFileSplit split = {0};
    bool in_argument = false;
    char8 quote = 0;
    u64 argument_start = 0;
    if (string_first_code_unit(content, 0) != BUSTER_STRING_NO_MATCH)
    {
        split.failure = S8("response file {S8} contains a NUL byte");
    }
    for (u64 index = 0; index <= content.length && !split.failure.length; index += 1)
    {
        bool end = index == content.length;
        char8 byte = end ? (char8)0 : content.pointer[index];
        bool literal = false;
        bool separator = false;
        if (end)
        {
            separator = true;
            if (quote)
            {
                split.failure = S8("response file {S8} ends inside a quoted argument");
            }
        }
        else if (byte == '\\')
        {
            if (index + 1 < content.length)
            {
                index += 1;
                byte = content.pointer[index];
                literal = true;
            }
            else
            {
                split.failure = S8("response file {S8} ends with an unfinished backslash escape");
            }
        }
        else if (quote)
        {
            literal = byte != quote;
            if (!literal)
            {
                quote = 0;
            }
        }
        else if (byte == '"' || byte == '\'')
        {
            quote = byte;
        }
        else
        {
            separator = byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\v' || byte == '\f';
            literal = !separator;
        }
        if (!in_argument && (literal || quote))
        {
            in_argument = true;
            argument_start = split.byte_count;
        }
        if (literal)
        {
            if (byte == '@' && split.byte_count == argument_start)
            {
                split.failure = S8("response file {S8} names another response file; nested response files are not supported");
            }
            if (bytes)
            {
                bytes[split.byte_count] = byte;
            }
            split.byte_count += 1;
        }
        if (separator && in_argument && !split.failure.length)
        {
            if (arguments)
            {
                bytes[split.byte_count] = 0;
                arguments[split.argument_count] = (String8){.pointer = bytes + argument_start, .length = split.byte_count - argument_start};
            }
            split.byte_count += 1;
            split.argument_count += 1;
            in_argument = false;
        }
    }
    return split;
}

BUSTER_GLOBAL_LOCAL SliceString8 compiler_driver_expand_response_files(Arena* arena, CompilerDriverInvocation* invocation, SliceString8 arguments)
{
    SliceString8 result = arguments;
    u64 response_file_count = 0;
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        String8 argument = arguments.pointer[index];
        response_file_count += (u64)(argument.length && argument.pointer[0] == '@');
    }
    if (response_file_count)
    {
        // Every file is read and counted before the expanded array is sized;
        // the second split copies arguments out of the retained contents.
        String8* contents = arena_allocate(arena, String8, arguments.length);
        CompilerDriverResponseFileSplit* splits = arena_allocate(arena, CompilerDriverResponseFileSplit, arguments.length);
        u64 argument_count = arguments.length - response_file_count;
        u64 content_bytes = 0;
        for (u64 index = 0; index < arguments.length && invocation->error == COMPILER_DRIVER_ERROR_NONE; index += 1)
        {
            String8 argument = arguments.pointer[index];
            if (argument.length && argument.pointer[0] == '@')
            {
                String8 path = string_slice(argument, 1, argument.length);
                if (!path.length)
                {
                    invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
                    invocation->diagnostic = S8("expected a response file path after @");
                }
                else
                {
                    path = string_duplicate_arena(arena, path, true);
                    CompilerDriverResponseFileStatus status =
                        compiler_driver_response_file_read(arena, path, COMPILER_DRIVER_RESPONSE_FILE_BYTE_LIMIT - content_bytes, &contents[index]);
                    if (status == COMPILER_DRIVER_RESPONSE_FILE_UNREADABLE)
                    {
                        invocation->error = COMPILER_DRIVER_ERROR_FILE_READ;
                        invocation->diagnostic = string_format(arena, S8("could not read response file {S8}"), path);
                    }
                    else if (status == COMPILER_DRIVER_RESPONSE_FILE_OVERSIZED)
                    {
                        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
                        invocation->diagnostic = string_format(arena, S8("response files exceed the {u64}-byte limit at {S8}"),
                                                               (u64)COMPILER_DRIVER_RESPONSE_FILE_BYTE_LIMIT, path);
                    }
                    else
                    {
                        content_bytes += contents[index].length;
                        splits[index] = compiler_driver_response_file_split(contents[index], 0, 0);
                        argument_count += splits[index].argument_count;
                        if (splits[index].failure.length)
                        {
                            compiler_driver_argument_error(arena, invocation, splits[index].failure, path);
                        }
                        else if (argument_count > COMPILER_DRIVER_RESPONSE_FILE_ARGUMENT_LIMIT)
                        {
                            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
                            invocation->diagnostic = string_format(arena, S8("response files expand the command line past {u64} arguments at {S8}"),
                                                                   (u64)COMPILER_DRIVER_RESPONSE_FILE_ARGUMENT_LIMIT, path);
                        }
                    }
                }
            }
        }
        result = (SliceString8){0};
        if (invocation->error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8* expanded = arena_allocate(arena, String8, argument_count);
            u64 expanded_count = 0;
            if (!expanded && argument_count)
            {
                invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
                invocation->diagnostic = S8("could not allocate expanded response-file arguments");
            }
            for (u64 index = 0; expanded && index < arguments.length; index += 1)
            {
                String8 argument = arguments.pointer[index];
                if (argument.length && argument.pointer[0] == '@')
                {
                    char8* bytes = arena_allocate(arena, char8, splits[index].byte_count);
                    if (bytes || !splits[index].byte_count)
                    {
                        compiler_driver_response_file_split(contents[index], expanded + expanded_count, bytes);
                        expanded_count += splits[index].argument_count;
                    }
                    else
                    {
                        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
                        invocation->diagnostic = S8("could not allocate expanded response-file arguments");
                    }
                }
                else
                {
                    expanded[expanded_count++] = argument;
                }
            }
            result = expanded && invocation->error == COMPILER_DRIVER_ERROR_NONE ? (SliceString8){.pointer = expanded, .length = expanded_count} : (SliceString8){0};
        }
    }
    return result;
}

// Kept equal to the __clang_major__/__clang_minor__/__clang_patchlevel__ and
// __clang_version__ predefines of the C frontend (c_source.c); a driver test
// compares the two.
#define COMPILER_DRIVER_VERSION_STRING "18.0.0"

// The target in the GNU triple shape `arch-os[-environment]` that -target
// accepts back: Linux names its GNU environment, Windows the MSVC ABI.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_query_machine(Arena* arena, CompilerDriverInvocation const* invocation)
{
    String8 result;
    if (invocation->has_gpu_target)
    {
        result = gpu_target_to_string(arena, invocation->gpu_target);
    }
    else
    {
        String8 environment = invocation->target.os == OPERATING_SYSTEM_LINUX ? S8("-gnu") :
                              invocation->target.os == OPERATING_SYSTEM_WINDOWS ? S8("-msvc") : S8("");
        result = string_format(arena, S8("{S8}-{S8}{S8}"), cpu_arch_to_string_os(invocation->target.cpu_arch),
                               operating_system_to_string_os(invocation->target.os), environment);
    }
    return result;
}

String8 compiler_driver_query_text(Arena* arena, CompilerDriverInvocation const* invocation)
{
    String8 result = {0};
    if (invocation->query == COMPILER_DRIVER_QUERY_VERSION)
    {
        result = string_format(arena, S8("Buster clang version " COMPILER_DRIVER_VERSION_STRING " (buster)\nTarget: {S8}\nThread model: posix\n"),
                               compiler_driver_query_machine(arena, invocation));
    }
    else if (invocation->query == COMPILER_DRIVER_QUERY_DUMP_VERSION)
    {
        result = S8(COMPILER_DRIVER_VERSION_STRING "\n");
    }
    else if (invocation->query == COMPILER_DRIVER_QUERY_DUMP_MACHINE)
    {
        result = string_format(arena, S8("{S8}\n"), compiler_driver_query_machine(arena, invocation));
    }
    return result;
}

CompilerDriverInvocation compiler_driver_parse_arguments(Arena* arena, SliceString8 arguments)
{
    CompilerDriverInvocation invocation = {
        .target = target_native,
        .language = COMPILER_DRIVER_LANGUAGE_AUTOMATIC,
        .action = COMPILER_DRIVER_ACTION_LINK,
        .c_dialect = COMPILER_DRIVER_C_DIALECT_GNU17,
        // Source debug information is opt-in; ordinary artifact generation
        // should not construct debug models or pay their output cost.
        .debug_info = false,
        // Native machine code needs register placement even when source-level
        // optimization is disabled. Match LLVM's -O0 policy by using the
        // low-latency allocator unless the caller explicitly opts out.
        .register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST,
        // The bounded canonical pipeline passed its dedicated-host total-time
        // and peak-RSS adoption gates. Keep every pass independently
        // selectable below, including a whole-pipeline opt-out.
        .fast_passes = IR_FAST_ALL,
    };
    if (!arena)
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        return invocation;
    }
    // A failed expansion leaves no arguments; every later step is gated on
    // invocation.error, so the response-file diagnostic is the one reported.
    arguments = compiler_driver_expand_response_files(arena, &invocation, arguments);
    // At most one resource directory plus four sysroot directories. Native
    // Windows INCLUDE entries reserve their own exact-sized array below.
    u64 default_include_capacity = 5;
    u64 linker_argument_capacity = arguments.length;
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        String8 argument = arguments.pointer[index];
        if (string_starts_with_sequence(argument, S8("-Wl,")))
        {
            for (u64 offset = 4; offset < argument.length; offset += 1)
            {
                linker_argument_capacity += argument.pointer[offset] == ',';
            }
        }
    }
    if (arguments.length > UINT32_MAX - default_include_capacity || linker_argument_capacity > UINT32_MAX)
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation.diagnostic = S8("too many compiler arguments");
        return invocation;
    }
    invocation.input_paths = arena_allocate(arena, String8, arguments.length);
    invocation.input_languages = arena_allocate(arena, CompilerDriverLanguage, arguments.length);
    invocation.link_operations = arena_allocate(arena, CompilerDriverLinkOperation, arguments.length);
    invocation.include_paths = arena_allocate(arena, String8, arguments.length);
    invocation.system_include_paths = arena_allocate(arena, String8, arguments.length + default_include_capacity);
    invocation.macro_operations = arena_allocate(arena, CPreprocessorOperation, arguments.length);
    invocation.library_paths = arena_allocate(arena, String8, arguments.length);
    invocation.libraries = arena_allocate(arena, String8, arguments.length);
    invocation.framework_paths = arena_allocate(arena, String8, arguments.length);
    invocation.frameworks = arena_allocate(arena, String8, arguments.length);
    invocation.linker_arguments = arena_allocate(arena, String8, linker_argument_capacity);
    invocation.gpu_arguments = arena_allocate(arena, String8, arguments.length);
    u64 feature_override_capacity = 0;
    for (u64 argument_index = 0; argument_index < arguments.length; argument_index += 1)
    {
        feature_override_capacity += arguments.pointer[argument_index].length / 2 + 1;
    }
    CompilerDriverFeatureOverride* feature_overrides = arena_allocate(arena, CompilerDriverFeatureOverride, feature_override_capacity);
    u64 feature_override_count = 0;
    String8 architecture_option = {0};
    bool options_ended = false;
    bool action_seen = false;
    // Whether the code model in force came from -fPIE/-fpie, which is what
    // -fno-pie cancels; -fno-pie after -fPIC leaves -fPIC's model alone.
    bool position_independent_executable_model = false;
    String8 position_independent_code_option = {0};
    // The -shared or -pie spelling that asked for a position-independent
    // image, kept for the diagnostic on a target with no writer for one.
    String8 position_independent_image_option = {0};
    bool common_storage_requested = false;
    bool static_link_requested = false;
    for (u64 argument_index = 0; argument_index < arguments.length && invocation.error == COMPILER_DRIVER_ERROR_NONE; argument_index += 1)
    {
        String8 argument = arguments.pointer[argument_index];
        // A lone `-` names standard input, as it does for GCC and Clang.
        if (options_ended || !argument.length || argument.pointer[0] != '-' || argument.length == 1)
        {
            u32 input_index = invocation.input_count++;
            invocation.input_paths[input_index] = argument;
            invocation.input_languages[input_index] = invocation.language;
            invocation.input_language_count = invocation.input_count;
            invocation.link_operations[invocation.link_operation_count++] = (CompilerDriverLinkOperation){
                .index = input_index, .kind = COMPILER_DRIVER_LINK_OPERATION_FILE,
            };
            continue;
        }
        if (string_equal(argument, S8("--")))
        {
            options_ended = true;
            continue;
        }
        if (string_equal(argument, S8("-E")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_PREPROCESS)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_PREPROCESS;
            continue;
        }
        if (string_equal(argument, S8("-S")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_ASSEMBLY)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_ASSEMBLY;
            continue;
        }
        if (string_equal(argument, S8("-c")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_OBJECT)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_OBJECT;
            continue;
        }
        if (string_equal(argument, S8("-fsyntax-only")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_SYNTAX_ONLY;
            continue;
        }
        if (string_equal(argument, S8("-emit-llvm")))
        {
            invocation.emit_llvm_bitcode = true;
            continue;
        }
        if (string_equal(argument, S8("-fsource-cache")) || string_equal(argument, S8("-fno-source-cache")))
        {
            invocation.enable_source_cache = string_equal(argument, S8("-fsource-cache"));
            continue;
        }
        String8 compile_jobs_prefix = S8("-fcompile-jobs=");
        if (string_starts_with_sequence(argument, compile_jobs_prefix))
        {
            String8 value = {.pointer = argument.pointer + compile_jobs_prefix.length,
                             .length = argument.length - compile_jobs_prefix.length};
            IntegerParsingU64 parsed = string8_parse_u64_decimal(value);
            if (parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == value.length && parsed.value && parsed.value <= UINT32_MAX)
            {
                invocation.compile_jobs = (u32)parsed.value;
            }
            else
            {
                compiler_driver_argument_error(arena, &invocation, S8("expected a positive 32-bit compile job count: {S8}"), argument);
            }
            continue;
        }
        if (string_equal(argument, S8("--version")) || string_equal(argument, S8("-dumpversion")) || string_equal(argument, S8("-dumpmachine")))
        {
            invocation.query = string_equal(argument, S8("--version")) ? COMPILER_DRIVER_QUERY_VERSION :
                               string_equal(argument, S8("-dumpversion")) ? COMPILER_DRIVER_QUERY_DUMP_VERSION : COMPILER_DRIVER_QUERY_DUMP_MACHINE;
            continue;
        }
        // GCC and Clang spell "no warnings" -w. The only warnings the driver
        // publishes are the preprocessor's (#warning), and they are gated at
        // compiler_driver_publish_c_diagnostics.
        if (string_equal(argument, S8("-w")))
        {
            invocation.suppress_warnings = true;
            continue;
        }
        if (string_equal(argument, S8("-v")) || string_equal(argument, S8("--verbose")))
        {
            invocation.verbose = true;
            continue;
        }
        if (string_equal(argument, S8("-g")) || string_equal(argument, S8("-g0")))
        {
            invocation.debug_info = !string_equal(argument, S8("-g0"));
            invocation.debug_info_explicit = true;
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-g")))
        {
            compiler_driver_argument_error(arena, &invocation, S8("unsupported debug option: {S8}"), argument);
            break;
        }
        if (string_equal(argument, S8("-dM")))
        {
            invocation.dump_macros = true;
            continue;
        }
        if (string_equal(argument, S8("-nostdinc")))
        {
            invocation.no_standard_includes = true;
            continue;
        }
        if (string_equal(argument, S8("--save-temps")) || string_equal(argument, S8("-save-temps")))
        {
            invocation.save_gpu_temporaries = true;
            continue;
        }
        if (string_equal(argument, S8("-o")) || string_equal(argument, S8("-e")) || string_equal(argument, S8("--entry")) ||
            string_equal(argument, S8("-I")) || string_equal(argument, S8("-isystem")) ||
            string_equal(argument, S8("-D")) || string_equal(argument, S8("-U")) || string_equal(argument, S8("-L")) || string_equal(argument, S8("-l")) ||
            string_equal(argument, S8("-F")) || string_equal(argument, S8("-framework")) || string_equal(argument, S8("-x")) ||
            string_equal(argument, S8("-target")) || string_equal(argument, S8("--target")) || string_equal(argument, S8("-march")) ||
            string_equal(argument, S8("-mcpu")) || string_equal(argument, S8("-mattr")) || string_equal(argument, S8("-masm")) ||
            string_equal(argument, S8("-isysroot")) || string_equal(argument, S8("--sysroot")) ||
            string_equal(argument, S8("-Xlinker")) ||
            string_equal(argument, S8("--gpu-arch")) || string_equal(argument, S8("--gpu-stage")) || string_equal(argument, S8("--gpu-entry")) ||
            string_equal(argument, S8("--shader-model")) || string_equal(argument, S8("--metal-sdk")) || string_equal(argument, S8("--cuda-path")) ||
            string_equal(argument, S8("--rocm-path")) || string_equal(argument, S8("--gpu-clang")) || string_equal(argument, S8("--gpu-llc")) ||
            string_equal(argument, S8("--spirv-link")) || string_equal(argument, S8("--spirv-dis")) || string_equal(argument, S8("--xcrun")) ||
            string_equal(argument, S8("--dxc")) || string_equal(argument, S8("-Xgpu")))
        {
            if (argument_index + 1 >= arguments.length)
            {
                compiler_driver_argument_error(arena, &invocation, S8("missing argument after {S8}"), argument);
                break;
            }
            String8 value = arguments.pointer[++argument_index];
            if (string_equal(argument, S8("-o")))
            {
                invocation.output_path = value;
            }
            else if (string_equal(argument, S8("-e")) || string_equal(argument, S8("--entry")))
            {
                invocation.entry_symbol = value;
            }
            else if (string_equal(argument, S8("-I")))
            {
                invocation.include_paths[invocation.include_path_count++] = value;
            }
            else if (string_equal(argument, S8("-isystem")))
            {
                invocation.system_include_paths[invocation.system_include_path_count++] = value;
            }
            else if (string_equal(argument, S8("-D")))
            {
                invocation.macro_operations[invocation.macro_operation_count++] = (CPreprocessorOperation){
                    .operand = value,
                    .kind = C_PREPROCESSOR_OPERATION_DEFINE,
                };
            }
            else if (string_equal(argument, S8("-U")))
            {
                invocation.macro_operations[invocation.macro_operation_count++] = (CPreprocessorOperation){
                    .operand = value,
                    .kind = C_PREPROCESSOR_OPERATION_UNDEFINE,
                };
            }
            else if (string_equal(argument, S8("-L")))
            {
                invocation.library_paths[invocation.library_path_count++] = value;
            }
            else if (string_equal(argument, S8("-l")))
            {
                u32 library_index = invocation.library_count++;
                invocation.libraries[library_index] = value;
                invocation.link_operations[invocation.link_operation_count++] = (CompilerDriverLinkOperation){
                    .index = library_index, .kind = COMPILER_DRIVER_LINK_OPERATION_LIBRARY,
                };
            }
            else if (string_equal(argument, S8("-F")))
            {
                invocation.framework_paths[invocation.framework_path_count++] = value;
            }
            else if (string_equal(argument, S8("-framework")))
            {
                invocation.frameworks[invocation.framework_count++] = value;
            }
            else if (string_equal(argument, S8("-x")))
            {
                if (string_equal(value, S8("c")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_C;
                }
                else if (string_equal(value, S8("cpp-output")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT;
                }
                else if (string_equal(value, S8("cl")) || string_equal(value, S8("opencl")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_OPENCL;
                }
                else if (string_equal(value, S8("cuda")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_CUDA;
                }
                else if (string_equal(value, S8("hip")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_HIP;
                }
                else if (string_equal(value, S8("metal")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_METAL;
                }
                else if (string_equal(value, S8("hlsl")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_HLSL;
                }
                else if (string_equal(value, S8("ir")) || string_equal(value, S8("llvm-ir")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_LLVM_IR;
                }
                else if (string_equal(value, S8("spirv")) || string_equal(value, S8("spirv-binary")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_SPIRV_BINARY;
                }
                else if (string_equal(value, S8("air")) || string_equal(value, S8("metal-air")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_METAL_AIR;
                }
                else if (string_equal(value, S8("assembler")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_ASSEMBLY;
                }
                else if (string_equal(value, S8("none")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_AUTOMATIC;
                }
                else
                {
                    compiler_driver_argument_error(arena, &invocation, S8("unsupported language: {S8}"), value);
                    break;
                }
            }
            else if (string_equal(argument, S8("-target")) || string_equal(argument, S8("--target")))
            {
                if (!compiler_driver_set_target(arena, &invocation, value))
                {
                    break;
                }
            }
            else if (string_equal(argument, S8("-march")) || string_equal(argument, S8("-mcpu")))
            {
                architecture_option = value;
            }
            else if (string_equal(argument, S8("-mattr")))
            {
                if (!compiler_driver_parse_feature_overrides(arena, &invocation, value, feature_overrides, feature_override_capacity,
                                                             &feature_override_count))
                {
                    break;
                }
            }
            else if (string_equal(argument, S8("-masm")))
            {
                if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
                {
                    break;
                }
            }
            else if (string_equal(argument, S8("-isysroot")) || string_equal(argument, S8("--sysroot")))
            {
                invocation.sysroot = value;
            }
            else if (string_equal(argument, S8("--gpu-arch")))
            {
                invocation.gpu_architecture = value;
            }
            else if (string_equal(argument, S8("--gpu-stage")))
            {
                invocation.gpu_stage = value;
            }
            else if (string_equal(argument, S8("--gpu-entry")))
            {
                invocation.gpu_entry_point = value;
            }
            else if (string_equal(argument, S8("--shader-model")))
            {
                invocation.gpu_shader_model = value;
            }
            else if (string_equal(argument, S8("--metal-sdk")))
            {
                invocation.metal_sdk = value;
            }
            else if (string_equal(argument, S8("--cuda-path")))
            {
                invocation.cuda_path = value;
            }
            else if (string_equal(argument, S8("--rocm-path")))
            {
                invocation.rocm_path = value;
            }
            else if (string_equal(argument, S8("--gpu-clang")))
            {
                invocation.gpu_tools.clang_path = value;
            }
            else if (string_equal(argument, S8("--gpu-llc")))
            {
                invocation.gpu_tools.llc_path = value;
            }
            else if (string_equal(argument, S8("--spirv-link")))
            {
                invocation.gpu_tools.spirv_link_path = value;
            }
            else if (string_equal(argument, S8("--spirv-dis")))
            {
                invocation.gpu_tools.spirv_dis_path = value;
            }
            else if (string_equal(argument, S8("--xcrun")))
            {
                invocation.gpu_tools.xcrun_path = value;
            }
            else if (string_equal(argument, S8("--dxc")))
            {
                invocation.gpu_tools.dxc_path = value;
            }
            else if (string_equal(argument, S8("-Xgpu")))
            {
                invocation.gpu_arguments[invocation.gpu_argument_count++] = value;
            }
            else
            {
                invocation.linker_arguments[invocation.linker_argument_count++] = value;
            }
            continue;
        }
        String8 value = compiler_driver_option_value(argument, S8("--target="));
        if (!value.length)
        {
            value = compiler_driver_option_value(argument, S8("-target="));
        }
        if (value.length)
        {
            if (!compiler_driver_set_target(arena, &invocation, value))
            {
                break;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--sysroot="));
        if (value.length)
        {
            invocation.sysroot = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-arch="));
        if (value.length)
        {
            invocation.gpu_architecture = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-stage="));
        if (value.length)
        {
            invocation.gpu_stage = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-entry="));
        if (value.length)
        {
            invocation.gpu_entry_point = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--shader-model="));
        if (value.length)
        {
            invocation.gpu_shader_model = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--metal-sdk="));
        if (value.length)
        {
            invocation.metal_sdk = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--cuda-path="));
        if (value.length)
        {
            invocation.cuda_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--rocm-path="));
        if (value.length)
        {
            invocation.rocm_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-clang="));
        if (value.length)
        {
            invocation.gpu_tools.clang_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-llc="));
        if (value.length)
        {
            invocation.gpu_tools.llc_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--spirv-link="));
        if (value.length)
        {
            invocation.gpu_tools.spirv_link_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--spirv-dis="));
        if (value.length)
        {
            invocation.gpu_tools.spirv_dis_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--xcrun="));
        if (value.length)
        {
            invocation.gpu_tools.xcrun_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--dxc="));
        if (value.length)
        {
            invocation.gpu_tools.dxc_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-arg="));
        if (!value.length)
        {
            value = compiler_driver_option_value(argument, S8("-Xgpu="));
        }
        if (value.length)
        {
            invocation.gpu_arguments[invocation.gpu_argument_count++] = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--entry="));
        if (value.length)
        {
            invocation.entry_symbol = value;
            continue;
        }
        if (string_equal(argument, S8("-fverify-codegen")))
        {
            invocation.verify_codegen = true;
            continue;
        }
        if (string_equal(argument, S8("-funsigned-char")) || string_equal(argument, S8("-fsigned-char")))
        {
            invocation.plain_char_policy = string_equal(argument, S8("-funsigned-char")) ?
                                               TARGET_PLAIN_CHAR_POLICY_UNSIGNED : TARGET_PLAIN_CHAR_POLICY_SIGNED;
            invocation.plain_char_policy_explicit = true;
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-fsysv-unnamed-bitfields=")))
        {
            value = compiler_driver_option_value(argument, S8("-fsysv-unnamed-bitfields="));
            if (!string_equal(value, S8("integer")) && !string_equal(value, S8("padding")))
            {
                invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
                invocation.diagnostic = S8("-fsysv-unnamed-bitfields requires integer or padding");
                break;
            }
            invocation.sysv_unnamed_bitfields_integer = string_equal(value, S8("integer"));
            invocation.sysv_bitfield_abi_explicit = true;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fbootstrap-trace="));
        if (value.length)
        {
            invocation.bootstrap_trace_prefix = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-finvestigation="));
        if (value.length)
        {
            invocation.investigation_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-finvestigation-function="));
        if (value.length)
        {
            invocation.investigation_function = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fsource-metrics="));
        if (value.length)
        {
            invocation.source_metrics_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fmetrics-out="));
        if (value.length)
        {
            invocation.metrics_output_path = value;
            invocation.collect_input_metrics = true;
            continue;
        }
        if (string_equal(argument, S8("-fmetrics-functions")))
        {
            invocation.collect_function_sizes = true;
            continue;
        }
        if (string_equal(argument, S8("-fkeep-going")) || string_equal(argument, S8("-fno-keep-going")))
        {
            invocation.keep_going = string_equal(argument, S8("-fkeep-going"));
            continue;
        }
        if (string_equal(argument, S8("-fno-frontend-ssa")) || string_equal(argument, S8("-ffrontend-ssa")))
        {
            invocation.disable_direct_ssa = string_equal(argument, S8("-fno-frontend-ssa"));
            continue;
        }
        if (string_equal(argument, S8("-fno-canonical-local-promotion")) || string_equal(argument, S8("-fcanonical-local-promotion")))
        {
            invocation.disable_local_promotion = string_equal(argument, S8("-fno-canonical-local-promotion"));
            continue;
        }
        if (string_equal(argument, S8("-fcanonical-fast")) || string_equal(argument, S8("-fno-canonical-fast")))
        {
            invocation.fast_passes = string_equal(argument, S8("-fcanonical-fast")) ? IR_FAST_ALL : 0;
            continue;
        }
        if (string_equal(argument, S8("-ftime-canonical-fast")))
        {
            invocation.measure_fast_passes = true;
            continue;
        }
        String8 fast_enable = compiler_driver_option_value(argument, S8("-fcanonical-fast-"));
        String8 fast_disable = compiler_driver_option_value(argument, S8("-fno-canonical-fast-"));
        if (fast_enable.length || fast_disable.length)
        {
            bool fast_option = false;
            for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1)
            {
                String8 name = ir_fast_pass_name((IrFastPass)pass);
                if (string_equal(fast_enable, name) || string_equal(fast_disable, name))
                {
                    if (fast_enable.length) invocation.fast_passes |= IR_FAST_PASS_BIT(pass);
                    else invocation.fast_passes &= ~IR_FAST_PASS_BIT(pass);
                    fast_option = true;
                }
            }
            if (fast_option) continue;
        }
        if (string_equal(argument, S8("-fno-target-local-promotion")) || string_equal(argument, S8("-ftarget-local-promotion")))
        {
            invocation.disable_target_local_promotion = string_equal(argument, S8("-fno-target-local-promotion"));
            continue;
        }
        if (string_equal(argument, S8("-fcommon")) || string_equal(argument, S8("-fno-common")))
        {
            common_storage_requested = string_equal(argument, S8("-fcommon"));
            continue;
        }
        // Register allocation is independent of source-level optimization:
        // like LLVM, -O0 still uses the low-latency allocator. QUALITY stays
        // out of the optimization-level mapping because it does not yet beat
        // FAST on a measured corpus; callers can still name it explicitly.
        if (string_equal(argument, S8("-fno-machine-fallback")) || string_equal(argument, S8("-fmachine-fallback")))
        {
            invocation.reject_machine_fallback = string_equal(argument, S8("-fno-machine-fallback"));
            continue;
        }
        if (string_equal(argument, S8("-fcodegen-fallback-census")) || string_equal(argument, S8("-fno-codegen-fallback-census")))
        {
            invocation.record_codegen_fallbacks = string_equal(argument, S8("-fcodegen-fallback-census"));
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-O")))
        {
            struct { String8 flag; u8 level; } levels[] = {
#define BUSTER_DRIVER_OPTIMIZATION(flag, level) {S8(flag), level},
                BUSTER_CODEGEN_OPTIMIZATIONS(BUSTER_DRIVER_OPTIMIZATION)
#undef BUSTER_DRIVER_OPTIMIZATION
            };
            bool found = false;
            for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(levels); index += 1)
            {
                if (string_equal(argument, levels[index].flag))
                {
                    invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST;
                    invocation.optimization_level = levels[index].level;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported optimization level: {S8}"), argument);
                break;
            }
            continue;
        }
        if (string_equal(argument, S8("-fno-register-allocator")))
        {
            invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_NONE;
            invocation.register_allocator_explicit = true;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fregister-allocator="));
        if (value.length)
        {
            struct { String8 name; u8 mode; } modes[] = {
#define BUSTER_DRIVER_ALLOCATOR(name, mode) {S8(name), mode},
                BUSTER_CODEGEN_ALLOCATORS(BUSTER_DRIVER_ALLOCATOR)
#undef BUSTER_DRIVER_ALLOCATOR
            };
            BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(modes) == CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT);
            bool found = false;
            for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
            {
                if (string_equal(value, modes[index].name))
                {
                    invocation.register_allocator = modes[index].mode;
                    invocation.register_allocator_explicit = true;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported register allocator: {S8}"), value);
                break;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-march="));
        if (!value.length)
        {
            value = compiler_driver_option_value(argument, S8("-mcpu="));
        }
        if (value.length)
        {
            architecture_option = value;
            continue;
        }
        // -mtune selects a scheduling model without changing the ISA or the
        // ABI. Instruction selection here has no per-CPU tuning, so every
        // spelling, native included, already describes the code it emits.
        if (compiler_driver_option_value(argument, S8("-mtune=")).length)
        {
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-mattr=")))
        {
            value = string_slice(argument, S8("-mattr=").length, argument.length);
            if (!compiler_driver_parse_feature_overrides(arena, &invocation, value, feature_overrides, feature_override_capacity, &feature_override_count))
            {
                break;
            }
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-masm=")))
        {
            value = string_slice(argument, S8("-masm=").length, argument.length);
            if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
            {
                break;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-std="));
        if (value.length)
        {
            if (!compiler_driver_set_dialect(&invocation, value))
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported C dialect: {S8}"), value);
                break;
            }
            invocation.c_dialect_explicit = true;
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-Wl,")))
        {
            u64 start = 4;
            for (u64 end = start; end <= argument.length && invocation.error == COMPILER_DRIVER_ERROR_NONE; end += 1)
            {
                if (end == argument.length || argument.pointer[end] == ',')
                {
                    if (end == start)
                    {
                        compiler_driver_argument_error(arena, &invocation, S8("empty linker argument in {S8}"), argument);
                    }
                    else
                    {
                        invocation.linker_arguments[invocation.linker_argument_count++] = string_slice(argument, start, end);
                    }
                    start = end + 1;
                }
            }
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-Wp,")) || string_starts_with_sequence(argument, S8("-Wa,")))
        {
            compiler_driver_argument_error(arena, &invocation, S8("unsupported pass-through option: {S8}"), argument);
            continue;
        }
        String8 prefix = {
            .pointer = argument.pointer,
            .length = BUSTER_MIN(argument.length, (u64)2),
        };
        value = argument.length > 2 ?
            (String8){
                .pointer = argument.pointer + 2,
                .length = argument.length - 2,
            } :
            (String8){0};
        if (string_equal(prefix, S8("-I")) && value.length)
        {
            invocation.include_paths[invocation.include_path_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-D")) && value.length)
        {
            invocation.macro_operations[invocation.macro_operation_count++] = (CPreprocessorOperation){
                .operand = value,
                .kind = C_PREPROCESSOR_OPERATION_DEFINE,
            };
            continue;
        }
        if (string_equal(prefix, S8("-U")) && value.length)
        {
            invocation.macro_operations[invocation.macro_operation_count++] = (CPreprocessorOperation){
                .operand = value,
                .kind = C_PREPROCESSOR_OPERATION_UNDEFINE,
            };
            continue;
        }
        if (string_equal(prefix, S8("-L")) && value.length)
        {
            invocation.library_paths[invocation.library_path_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-l")) && value.length)
        {
            u32 library_index = invocation.library_count++;
            invocation.libraries[library_index] = value;
            invocation.link_operations[invocation.link_operation_count++] = (CompilerDriverLinkOperation){
                .index = library_index, .kind = COMPILER_DRIVER_LINK_OPERATION_LIBRARY,
            };
            continue;
        }
        if (string_equal(prefix, S8("-F")) && value.length)
        {
            invocation.framework_paths[invocation.framework_path_count++] = value;
            continue;
        }
        bool optimization_option = argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'O';
        bool debug_option = argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'g';
        bool warning_option =
            argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'W' && !string_starts_with_sequence(argument, S8("-Wl,"));
        // The code model, which is a fact about the emitted references
        // rather than a flag to absorb. It decides the thread-local model,
        // because an object that may end up in a shared library cannot fold
        // an offset from the thread pointer, and it decides how every other
        // reference to an interposable symbol is spelled: through the GOT for
        // an address and the PLT for a direct call. -fno-pic asks for the
        // rip-relative forms back. -fPIE/-fpie select the same model: an
        // executable could bind its own definitions directly, but code that
        // may be linked into a shared object is also correct in a PIE, and
        // the position-independent image writer relaxes the GOT loads of
        // definitions it binds. The last of the four positive spellings wins,
        // as it does for GCC, and -fno-pie cancels only a PIE model.
        if (string_equal(argument, S8("-fPIC")) || string_equal(argument, S8("-fpic")) || string_equal(argument, S8("-fPIE")) ||
            string_equal(argument, S8("-fpie")))
        {
            invocation.position_independent_level = string_equal(argument, S8("-fPIC")) || string_equal(argument, S8("-fPIE")) ? 2 : 1;
            invocation.position_independent = true;
            position_independent_code_option = argument;
            position_independent_executable_model = string_equal(argument, S8("-fPIE")) || string_equal(argument, S8("-fpie"));
            invocation.position_independent_executable = position_independent_executable_model;
            continue;
        }
        if (string_equal(argument, S8("-fno-pic")) || (string_equal(argument, S8("-fno-pie")) && position_independent_executable_model))
        {
            invocation.position_independent_level = 0;
            invocation.position_independent = false;
            invocation.position_independent_executable = false;
            position_independent_code_option = (String8){0};
            position_independent_executable_model = false;
            continue;
        }
        // A static executable would carry libc's archive members and their
        // own startup in place of the dynamic loader, and no writer here
        // produces one: hosted ELF links import libc.so.6. Compiling alone
        // ignores the link option as GCC does; a link refuses it below.
        if (string_equal(argument, S8("-static")))
        {
            static_link_requested = true;
            continue;
        }
        // The image a link produces. -shared outranks -pie wherever the two
        // meet, as it does for GCC; -no-pie returns to the fixed-address
        // executable only from -pie.
        if (string_equal(argument, S8("-shared")))
        {
            invocation.image_kind = NATIVE_IMAGE_SHARED;
            position_independent_image_option = argument;
            continue;
        }
        if (string_equal(argument, S8("-pie")) || string_equal(argument, S8("-no-pie")))
        {
            bool pie = string_equal(argument, S8("-pie"));
            if (invocation.image_kind != NATIVE_IMAGE_SHARED)
            {
                invocation.image_kind = pie ? NATIVE_IMAGE_PIE : NATIVE_IMAGE_EXECUTABLE;
                position_independent_image_option = pie ? argument : (String8){0};
            }
            continue;
        }
        // clang's spelling of the flag configure scripts pass the linker as
        // `-Xlinker -export-dynamic`; both routes land in linker_arguments
        // and the hosted ELF writer reads it there.
        if (string_equal(argument, S8("-rdynamic")))
        {
            invocation.linker_arguments[invocation.linker_argument_count++] = S8("-export-dynamic");
            continue;
        }
        bool compatible_codegen_option =
            string_equal(argument, S8("-pipe")) || string_equal(argument, S8("-pthread")) ||
            string_equal(argument, S8("-fno-pie")) || string_equal(argument, S8("-fno-builtin")) ||
            string_equal(argument, S8("-fwrapv")) || string_equal(argument, S8("-fno-strict-overflow")) || string_equal(argument, S8("-fno-strict-aliasing")) || string_equal(argument, S8("-funsigned-char")) ||
            string_equal(argument, S8("-fsigned-char")) ||
            // Buster emits no stack-protector prologue, so the disabling
            // spelling is already what it does. A libc asks for it on the
            // translation units that run before thread-local storage exists,
            // where the canary's own load would fault; accepting the flag lets
            // one flag set drive this compiler and the reference one.
            string_equal(argument, S8("-fno-stack-protector"));
        if (optimization_option || debug_option || warning_option || compatible_codegen_option)
        {
            continue;
        }
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8}"), argument);
        break;
    }
    // Standard input has no suffix to classify, so, as for Clang, it needs an
    // explicit C language or -E, which reads it as C source. It is one stream
    // and can be consumed once.
    u32 standard_input_count = 0;
    for (u32 input_index = 0; input_index < invocation.input_count && invocation.error == COMPILER_DRIVER_ERROR_NONE; input_index += 1)
    {
        if (string_equal(invocation.input_paths[input_index], S8("-")))
        {
            standard_input_count += 1;
            if (invocation.input_languages[input_index] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC &&
                invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
            {
                invocation.input_languages[input_index] = COMPILER_DRIVER_LANGUAGE_C;
            }
            if (standard_input_count > 1)
            {
                compiler_driver_argument_error(arena, &invocation, S8("standard input {S8} can be named only once"), S8("-"));
            }
            else if (invocation.input_languages[input_index] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC)
            {
                compiler_driver_argument_error(arena, &invocation, S8("-E or -x c is required when input is from standard input {S8}"), S8("-"));
            }
            else if (!compiler_driver_c_input(invocation.input_languages[input_index], S8("-")))
            {
                compiler_driver_argument_error(arena, &invocation, S8("standard input {S8} is supported only for C source"), S8("-"));
            }
        }
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (invocation.has_gpu_target)
        {
            compiler_driver_reject_gpu_native_options(arena, &invocation, feature_override_count);
            if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
            {
                compiler_driver_resolve_gpu_target(arena, &invocation, architecture_option);
            }
            if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
            {
                compiler_driver_check_gpu_inputs(arena, &invocation);
            }
        }
        else
        {
            compiler_driver_resolve_native_target(arena, &invocation, architecture_option, feature_overrides, feature_override_count);
        }
    }
    // No canonical object field carries common-storage intent. Reject only an
    // effective request when this invocation will emit a code-generation artifact;
    // preprocessing and syntax-only checks have no storage representation to lose.
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && common_storage_requested &&
        invocation.action != COMPILER_DRIVER_ACTION_PREPROCESS && invocation.action != COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
    {
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8}"), S8("-fcommon"));
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.dump_macros && invocation.has_gpu_target && invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8} for a GPU target"), S8("-dM"));
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && static_link_requested && invocation.action == COMPILER_DRIVER_ACTION_LINK)
    {
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8} (static executables are not linked; hosted links import libc dynamically)"),
                                       S8("-static"));
    }
    compiler_driver_validate_spirv_invocation(&invocation);
    // Only the x86-64 Linux writer places a position-independent image. The
    // request is a link option, so a compile-only invocation carrying it in
    // shared flags is not refused for it, as GCC ignores it there.
    bool position_independent_image_target =
        !invocation.has_gpu_target && invocation.target.os == OPERATING_SYSTEM_LINUX && invocation.target.cpu_arch == CPU_ARCH_X86_64;
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.image_kind != NATIVE_IMAGE_EXECUTABLE &&
        invocation.action == COMPILER_DRIVER_ACTION_LINK && !position_independent_image_target)
    {
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8}"), position_independent_image_option);
    }
    // Code linked into a position-independent image in this invocation is
    // compiled for one: the fixed-address model's absolute and copy-relocated
    // references are what such an image cannot hold.
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.image_kind != NATIVE_IMAGE_EXECUTABLE &&
        invocation.action == COMPILER_DRIVER_ACTION_LINK)
    {
        if (!invocation.position_independent_level)
        {
            invocation.position_independent_level = 2;
        }
        invocation.position_independent = invocation.position_independent_level != 0;
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.plain_char_policy_explicit)
    {
        invocation.target.plain_char_policy = invocation.plain_char_policy;
    }
    compiler_driver_validate_codegen_request(&invocation, true);
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && !invocation.no_standard_includes && !invocation.has_gpu_target &&
        invocation.target.os != OPERATING_SYSTEM_UEFI && invocation.target.cpu_arch != CPU_ARCH_SPIRV_COMPUTE)
    {
        compiler_driver_append_system_includes(arena, &invocation);
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (!invocation.has_gpu_target && invocation.target.os == OPERATING_SYSTEM_UEFI &&
            invocation.target.cpu_arch != CPU_ARCH_X86_64 && invocation.target.cpu_arch != CPU_ARCH_AARCH64)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("UEFI output is supported only for x86_64 and aarch64 targets");
        }
        else if (!invocation.has_gpu_target && invocation.target.os == OPERATING_SYSTEM_UEFI && invocation.linker_argument_count)
        {
            compiler_driver_argument_error(arena, &invocation, S8("raw linker arguments are not supported for UEFI targets: {S8}"),
                                           invocation.linker_arguments[0]);
        }
        else if (!invocation.has_gpu_target && invocation.framework_count && invocation.target.os != OPERATING_SYSTEM_MACOS &&
                 invocation.target.os != OPERATING_SYSTEM_IOS)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("-framework is only supported for Apple targets");
        }
        else if (!invocation.input_count && invocation.query == COMPILER_DRIVER_QUERY_NONE)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("no input files");
        }
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.linker_argument_count)
    {
        String8 unsupported = invocation.linker_arguments[0];
        NativeExecutableLinkOptions options = {
            .linker_arguments = invocation.linker_arguments,
            .linker_argument_count = invocation.linker_argument_count,
            .image_kind = (u8)invocation.image_kind,
        };
        if (invocation.action != COMPILER_DRIVER_ACTION_LINK || invocation.has_gpu_target || invocation.emit_llvm_bitcode ||
            !link_validate_linker_arguments(invocation.target, options, true, &unsupported))
        {
            compiler_driver_argument_error(arena, &invocation, S8("unsupported linker argument for this output: {S8}"), unsupported);
        }
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.investigation_path.length)
    {
        // Frame each expanded argument by its byte length; quoted spaces or
        // response-file boundaries cannot make different argv lists coincide.
        ByteWriter configuration = byte_writer_make(arena_allocate(arena, u8, INVESTIGATION_TEXT_LIMIT), INVESTIGATION_TEXT_LIMIT);
        for (u64 index = 0; !configuration.overflow && index < arguments.length; index += 1)
        {
            String8 length = string_format(arena, S8("{u64}:"), arguments.pointer[index].length);
            byte_writer_emit_bytes(&configuration, length.pointer, length.length);
            byte_writer_emit_bytes(&configuration, arguments.pointer[index].pointer, arguments.pointer[index].length);
        }
        if (configuration.overflow)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("investigation configuration exceeds 4096 bytes");
        }
        else
        {
            invocation.investigation_configuration = (String8){.pointer = (char8*)configuration.bytes, .length = configuration.count};
        }
    }
    compiler_driver_validate_native_pic_invocation(arena, &invocation, position_independent_code_option);
    return invocation;

}

typedef enum CompilerDriverCInputPhase
{
    COMPILER_DRIVER_C_INPUT_RAW,
    COMPILER_DRIVER_C_INPUT_PREPROCESSED,
    COMPILER_DRIVER_C_INPUT_INVALID,
} CompilerDriverCInputPhase;

// One authority classifies both the C language and its starting phase.
// `-x cpp-output` applies to every suffix; explicit `-x c` is the raw-source
// escape hatch even for `.i`, while automatic mode derives the phase from the
// suffix. The parser snapshots this value beside each input.
BUSTER_GLOBAL_LOCAL CompilerDriverCInputPhase compiler_driver_c_input_phase(CompilerDriverLanguage language, String8 path)
{
    CompilerDriverCInputPhase result = COMPILER_DRIVER_C_INPUT_INVALID;
    if (language == COMPILER_DRIVER_LANGUAGE_C)
    {
        result = COMPILER_DRIVER_C_INPUT_RAW;
    }
    else if (language == COMPILER_DRIVER_LANGUAGE_CPP_OUTPUT)
    {
        result = COMPILER_DRIVER_C_INPUT_PREPROCESSED;
    }
    else if (language == COMPILER_DRIVER_LANGUAGE_AUTOMATIC && path.length >= 2 && path.pointer[path.length - 2] == '.')
    {
        if (path.pointer[path.length - 1] == 'c')
        {
            result = COMPILER_DRIVER_C_INPUT_RAW;
        }
        else if (path.pointer[path.length - 1] == 'i')
        {
            result = COMPILER_DRIVER_C_INPUT_PREPROCESSED;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_c_input(CompilerDriverLanguage language, String8 path)
{
    return compiler_driver_c_input_phase(language, path) != COMPILER_DRIVER_C_INPUT_INVALID;
}

// A `.s` input, or any input under `-x assembler`.
BUSTER_GLOBAL_LOCAL bool compiler_driver_assembly_input(CompilerDriverLanguage language, String8 path)
{
    if (language == COMPILER_DRIVER_LANGUAGE_ASSEMBLY)
    {
        return true;
    }
    if (language != COMPILER_DRIVER_LANGUAGE_AUTOMATIC || path.length < 2)
    {
        return false;
    }
    return path.pointer[path.length - 2] == '.' && path.pointer[path.length - 1] == 's';
}

// GNU's `.S` runs the C preprocessor over assembly text before assembling
// it; compiler_driver_execute_preprocessed_assembly_single is that route.
BUSTER_GLOBAL_LOCAL bool compiler_driver_preprocessed_assembly_input(String8 path)
{
    return path.length >= 2 && path.pointer[path.length - 2] == '.' && path.pointer[path.length - 1] == 'S';
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_object_input(String8 path)
{
    if (path.length >= 2 && path.pointer[path.length - 2] == '.' && path.pointer[path.length - 1] == 'o')
    {
        return true;
    }
    return path.length >= 4 && path.pointer[path.length - 4] == '.' && (path.pointer[path.length - 3] == 'o' || path.pointer[path.length - 3] == 'O') &&
           (path.pointer[path.length - 2] == 'b' || path.pointer[path.length - 2] == 'B') &&
           (path.pointer[path.length - 1] == 'j' || path.pointer[path.length - 1] == 'J');
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_input(String8 path)
{
    if (path.length >= 2 && path.pointer[path.length - 2] == '.' && (path.pointer[path.length - 1] == 'a' || path.pointer[path.length - 1] == 'A'))
    {
        return true;
    }
    return path.length >= 4 && path.pointer[path.length - 4] == '.' && (path.pointer[path.length - 3] == 'l' || path.pointer[path.length - 3] == 'L') &&
           (path.pointer[path.length - 2] == 'i' || path.pointer[path.length - 2] == 'I') &&
           (path.pointer[path.length - 1] == 'b' || path.pointer[path.length - 1] == 'B');
}

// Refuse a model the native AArch64 ELF emitter cannot represent before any
// source is mapped or output is published. Assembly and prebuilt inputs have
// their own relocation spelling; only an actual C generation route needs it.
BUSTER_GLOBAL_LOCAL void compiler_driver_validate_native_pic_invocation(Arena* arena, CompilerDriverInvocation* invocation, String8 option)
{
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->position_independent &&
        invocation->target.cpu_arch == CPU_ARCH_AARCH64 && object_format_for_target(invocation->target) == OBJECT_FORMAT_ELF64 &&
        !invocation->has_gpu_target && !invocation->emit_llvm_bitcode &&
        (invocation->action == COMPILER_DRIVER_ACTION_OBJECT || invocation->action == COMPILER_DRIVER_ACTION_ASSEMBLY ||
         invocation->action == COMPILER_DRIVER_ACTION_LINK))
    {
        bool c_input = false;
        for (u32 input_index = 0; invocation->input_paths && input_index < invocation->input_count && !c_input; input_index += 1)
        {
            String8 path = invocation->input_paths[input_index];
            CompilerDriverLanguage language = compiler_driver_input_language(*invocation, input_index);
            c_input = compiler_driver_c_input(language, path) && !compiler_driver_object_input(path) &&
                !compiler_driver_archive_input(path) && !compiler_driver_assembly_input(language, path) &&
                !compiler_driver_preprocessed_assembly_input(path);
        }
        if (c_input)
        {
            invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation->diagnostic = option.length ? string_format(arena, S8("unsupported option: {S8} on AArch64 ELF"), option) :
                S8("position-independent code generation is unsupported on AArch64 ELF");
        }
    }
    return;
}

typedef struct CompilerDriverDynamicLibraries CompilerDriverDynamicLibraries;
struct CompilerDriverDynamicLibraries
{
    NativeDynamicLibrary* pointer;
    NativeDynamicLibrary runtime;
    FileMapRead* export_maps;
    u32 count;
    u32 export_map_count;
    // The first `-l` request the export scan found no usable file for, in the
    // caller's own spelling.  A hosted ELF link must refuse such a request the
    // way ld does ("cannot find -lX"): recording a DT_NEEDED for a library
    // that exists nowhere on the search path defers the failure to the
    // loader, and a configure script reads the successful link as the
    // library existing.  Empty when every requested library was found.
    String8 missing_request;
    String8 unsupported_script_path;
    String8 missing_runtime_symbol;
    String8 runtime_failure_library;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u16(ByteSlice bytes, u64 offset, u16* value)
{
    bool result;
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u32(ByteSlice bytes, u64 offset, u32* value)
{
    bool result;
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u64(ByteSlice bytes, u64 offset, u64* value)
{
    bool result;
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_pe_rva_offset(ByteSlice bytes, u64 section_table, u16 section_count, u32 rva, u64* offset_out)
{
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * 40;
        u32 virtual_size = 0;
        u32 virtual_address = 0;
        u32 raw_size = 0;
        u32 raw_offset = 0;
        if (!compiler_driver_read_u32(bytes, section + 8, &virtual_size) || !compiler_driver_read_u32(bytes, section + 12, &virtual_address) ||
            !compiler_driver_read_u32(bytes, section + 16, &raw_size) || !compiler_driver_read_u32(bytes, section + 20, &raw_offset))
        {
            return false;
        }
        u64 span = BUSTER_MAX(virtual_size, raw_size);
        if (rva < virtual_address || (u64)rva - virtual_address >= span)
        {
            continue;
        }
        u64 offset = raw_offset + ((u64)rva - virtual_address);
        if (offset >= bytes.length)
        {
            return false;
        }
        *offset_out = offset;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_pe_library_exports(Arena* arena, CompilerDriverInvocation invocation, NativeDynamicLibrary* library,
                                                            FileMapRead* export_map)
{
    String8 path = {0};
    FileMapRead file = {0};
    ByteSlice bytes = {0};
    *export_map = (FileMapRead){0};

    for (u32 path_index = 0; path_index < invocation.library_path_count && !bytes.pointer; path_index += 1)
    {
        if (file.bytes.pointer)
        {
            file_map_unmap(file);
        }
        path = string_format_z(arena, S8("{S8}/{S8}"), invocation.library_paths[path_index], library->name);
        file = file_map_read(arena, path, (FileReadOptions){0});
        bytes = file.bytes;
    }
    if (!bytes.pointer && invocation.sysroot.length)
    {
        path = string_format_z(arena, S8("{S8}/Windows/System32/{S8}"), invocation.sysroot, library->name);
        file_map_unmap(file);
        file = file_map_read(arena, path, (FileReadOptions){0});
        bytes = file.bytes;
    }
#if BUSTER_WINDOWS
    if (!bytes.pointer && invocation.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        String8 system_root = os_get_environment_variable(S8("SystemRoot"));
        if (system_root.length)
        {
            path = string_format_z(arena, S8("{S8}/System32/{S8}"), system_root, library->name);
            file_map_unmap(file);
            file = file_map_read(arena, path, (FileReadOptions){0});
            bytes = file.bytes;
        }
    }
#endif
    if (!bytes.pointer)
    {
        file_map_unmap(file);
        file = file_map_read(arena, string_duplicate_arena(arena, library->name, true), (FileReadOptions){0});
        bytes = file.bytes;
    }
    u32 pe_offset = 0;
    u16 section_count = 0;
    u16 optional_size = 0;
    if (!bytes.pointer || bytes.length < 0x40 || bytes.pointer[0] != 'M' || bytes.pointer[1] != 'Z' || !compiler_driver_read_u32(bytes, 0x3c, &pe_offset) ||
        pe_offset > bytes.length || bytes.length - pe_offset < 24 || memcmp(bytes.pointer + pe_offset, "PE\0\0", 4) != 0 ||
        !compiler_driver_read_u16(bytes, pe_offset + 6, &section_count) || !compiler_driver_read_u16(bytes, pe_offset + 20, &optional_size))
    {
        file_map_unmap(file);
        return;
    }
    u64 optional = pe_offset + 24;
    u16 magic = 0;
    if (!compiler_driver_read_u16(bytes, optional, &magic))
    {
        file_map_unmap(file);
        return;
    }
    u64 directory = optional + (magic == 0x20b ? 112 : 96);
    u32 export_rva = 0;
    if ((magic != 0x20b && magic != 0x10b) || directory + 8 > optional + optional_size || !compiler_driver_read_u32(bytes, directory, &export_rva))
    {
        file_map_unmap(file);
        return;
    }
    library->exports_known = true;
    if (!export_rva)
    {
        *export_map = file;
        return;
    }
    u64 section_table = optional + optional_size;
    u64 export_offset = 0;
    u32 name_count = 0;
    u32 names_rva = 0;
    if (!compiler_driver_pe_rva_offset(bytes, section_table, section_count, export_rva, &export_offset) ||
        !compiler_driver_read_u32(bytes, export_offset + 24, &name_count) || !compiler_driver_read_u32(bytes, export_offset + 32, &names_rva) ||
        name_count > (bytes.length / sizeof(u32)))
    {
        file_map_unmap(file);
        return;
    }
    u64 names_offset = 0;
    if (!compiler_driver_pe_rva_offset(bytes, section_table, section_count, names_rva, &names_offset) || names_offset > bytes.length ||
        (u64)name_count * sizeof(u32) > bytes.length - names_offset)
    {
        file_map_unmap(file);
        return;
    }
    library->exported_symbols = arena_allocate(arena, String8, name_count);
    for (u32 name_index = 0; name_index < name_count; name_index += 1)
    {
        u32 name_rva = 0;
        u64 name_offset = 0;
        if (!compiler_driver_read_u32(bytes, names_offset + (u64)name_index * sizeof(u32), &name_rva) ||
            !compiler_driver_pe_rva_offset(bytes, section_table, section_count, name_rva, &name_offset))
        {
            continue;
        }
        u64 length = 0;
        while (name_offset + length < bytes.length && bytes.pointer[name_offset + length])
        {
            length += 1;
        }
        if (name_offset + length >= bytes.length)
        {
            continue;
        }
        library->exported_symbols[library->exported_symbol_count++] = (String8){
            .pointer = (char8*)bytes.pointer + name_offset,
            .length = length,
        };
    }
    *export_map = file;
}

// What one ELF shared library defines, read from its own dynamic symbol table.
//
// Two things the referencing name alone does not carry.  The data objects, for
// copy relocations: the address tells the linker which exported names are one
// object, so the executable can define every one of them at the slot it
// reserves, and the size tells it how large that slot has to be.  Only data is
// collected there; imported functions go through the PLT.
//
// And the symbol version of every definition, functions included.  A reference
// GNU ld resolves records the version it bound to, so the image keeps that
// answer for its whole life; an unversioned reference to a name whose
// definitions are all `name@VER` has no default to bind to at all, which is
// what makes GNU ld refuse `sys_errlist`.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_dynamic_symbols(Arena* arena, ByteSlice bytes, u16 machine, bool collect_data, NativeDynamicLibrary* library)
{
    enum
    {
        DRIVER_ELF_SECTION_HEADER_SIZE = 64,
        DRIVER_ELF_SYMBOL_SIZE = 24,
        DRIVER_ELF_SECTION_TYPE_DYNAMIC_SYMBOLS = 11,
        DRIVER_ELF_SECTION_TYPE_VERSION_DEFINITIONS = 0x6ffffffd,
        DRIVER_ELF_SECTION_TYPE_VERSION_SYMBOLS = 0x6fffffff,
        DRIVER_ELF_VERSION_DEFINITION_SIZE = 20,
        DRIVER_ELF_VERSION_AUXILIARY_SIZE = 8,
        DRIVER_ELF_VERSION_HIDDEN = 0x8000,
        DRIVER_ELF_VERSION_INDEX_MAX = 0x7fff,
        DRIVER_ELF_TYPE_SHARED = 3,
        DRIVER_ELF_SYMBOL_TYPE_OBJECT = 1,
        DRIVER_ELF_SECTION_ABSOLUTE = 0xfff1,
    };
    bool result = false;
    u16 type = 0;
    u16 file_machine = 0;
    u64 section_table = 0;
    u16 section_entry_size = 0;
    u16 section_count = 0;
    bool header_valid = bytes.length >= DRIVER_ELF_SECTION_HEADER_SIZE && memcmp(bytes.pointer, "\x7f" "ELF", 4) == 0 && bytes.pointer[4] == 2 &&
                        bytes.pointer[5] == 1 && compiler_driver_read_u16(bytes, 16, &type) && compiler_driver_read_u16(bytes, 18, &file_machine) &&
                        compiler_driver_read_u64(bytes, 40, &section_table) && compiler_driver_read_u16(bytes, 58, &section_entry_size) &&
                        compiler_driver_read_u16(bytes, 60, &section_count) && type == DRIVER_ELF_TYPE_SHARED && file_machine == machine &&
                        section_entry_size == DRIVER_ELF_SECTION_HEADER_SIZE && section_count && section_table <= bytes.length &&
                        (u64)section_count * DRIVER_ELF_SECTION_HEADER_SIZE <= bytes.length - section_table;
    u64 symbol_offset = 0;
    u64 symbol_size = 0;
    u64 string_offset = 0;
    u64 string_size = 0;
    u64 version_symbol_offset = 0;
    u64 version_symbol_size = 0;
    bool version_symbols_present = false;
    bool version_symbols_invalid = false;
    u64 version_definition_offset = 0;
    u64 version_definition_size = 0;
    u32 version_definition_count = 0;
    for (u16 section_index = 0; header_valid && section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * DRIVER_ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        u32 string_section = 0;
        u32 definition_count = 0;
        u64 offset = 0;
        u64 size = 0;
        u64 entry_size = 0;
        u64 strings_offset = 0;
        u64 strings_size = 0;
        bool type_valid = compiler_driver_read_u32(bytes, section + 4, &section_type);
        if (type_valid && section_type == DRIVER_ELF_SECTION_TYPE_VERSION_SYMBOLS)
        {
            version_symbols_invalid |= version_symbols_present;
            version_symbols_present = true;
        }
        if (!type_valid || !compiler_driver_read_u64(bytes, section + 24, &offset) ||
            !compiler_driver_read_u64(bytes, section + 32, &size) || offset > bytes.length || size > bytes.length - offset)
        {
            version_symbols_invalid |= type_valid && section_type == DRIVER_ELF_SECTION_TYPE_VERSION_SYMBOLS;
            continue;
        }
        if (section_type == DRIVER_ELF_SECTION_TYPE_DYNAMIC_SYMBOLS && !symbol_size)
        {
            if (compiler_driver_read_u32(bytes, section + 40, &string_section) && compiler_driver_read_u64(bytes, section + 56, &entry_size) &&
                entry_size == DRIVER_ELF_SYMBOL_SIZE && size && !(size % DRIVER_ELF_SYMBOL_SIZE) && string_section < section_count)
            {
                u64 strings = section_table + (u64)string_section * DRIVER_ELF_SECTION_HEADER_SIZE;
                if (compiler_driver_read_u64(bytes, strings + 24, &strings_offset) && compiler_driver_read_u64(bytes, strings + 32, &strings_size) &&
                    strings_offset <= bytes.length && strings_size <= bytes.length - strings_offset)
                {
                    symbol_offset = offset;
                    symbol_size = size;
                    string_offset = strings_offset;
                    string_size = strings_size;
                }
            }
        }
        else if (section_type == DRIVER_ELF_SECTION_TYPE_VERSION_SYMBOLS && !version_symbol_size)
        {
            version_symbol_offset = offset;
            version_symbol_size = size;
        }
        else if (section_type == DRIVER_ELF_SECTION_TYPE_VERSION_DEFINITIONS && !version_definition_size &&
                 compiler_driver_read_u32(bytes, section + 44, &definition_count) && definition_count)
        {
            version_definition_offset = offset;
            version_definition_size = size;
            version_definition_count = definition_count;
        }
    }
    // .gnu.version_d as an index-keyed table.  The definitions form a linked
    // list whose entries carry their own index, so the highest index decides
    // how large the table has to be; both walks are over the version count,
    // which is a few dozen entries even for glibc.  The names live in the
    // string table .dynsym already named: an ELF section table links both to
    // the one .dynstr.
    String8* version_names = 0;
    u32 version_name_count = 0;
    for (u32 pass = 0; pass < 2 && version_definition_count; pass += 1)
    {
        u64 cursor = version_definition_offset;
        u64 end = version_definition_offset + version_definition_size;
        u32 highest = 0;
        for (u32 definition = 0; definition < version_definition_count && cursor && cursor + DRIVER_ELF_VERSION_DEFINITION_SIZE <= end; definition += 1)
        {
            u16 index = 0;
            u16 flags = 0;
            u32 auxiliary = 0;
            u32 next = 0;
            if (!compiler_driver_read_u16(bytes, cursor + 2, &flags) || !compiler_driver_read_u16(bytes, cursor + 4, &index) ||
                !compiler_driver_read_u32(bytes, cursor + 12, &auxiliary) || !compiler_driver_read_u32(bytes, cursor + 16, &next))
            {
                break;
            }
            index = (u16)(index & DRIVER_ELF_VERSION_INDEX_MAX);
            // VER_FLG_BASE names the library itself rather than a version any
            // symbol is published under.
            bool base = (flags & 1) != 0;
            highest = !base && index > highest ? index : highest;
            u32 name = 0;
            u64 auxiliary_offset = cursor + auxiliary;
            if (pass && !base && index < version_name_count && auxiliary >= DRIVER_ELF_VERSION_DEFINITION_SIZE &&
                auxiliary_offset + DRIVER_ELF_VERSION_AUXILIARY_SIZE <= end && compiler_driver_read_u32(bytes, auxiliary_offset, &name) && name &&
                name < string_size)
            {
                u64 length = 0;
                while ((u64)name + length < string_size && bytes.pointer[string_offset + name + length])
                {
                    length += 1;
                }
                if (length && (u64)name + length < string_size)
                {
                    version_names[index] = (String8){.pointer = (char8*)bytes.pointer + string_offset + name, .length = length};
                }
            }
            cursor = next ? cursor + next : 0;
        }
        if (!pass)
        {
            version_name_count = highest + 1;
            version_names = arena_allocate(arena, String8, version_name_count);
            memset(version_names, 0, (u64)version_name_count * sizeof(*version_names));
        }
    }
    // Export arrays use u32 counts and the link indexes reserve UINT32_MAX
    // as an empty entry. Check before allocating or narrowing the ELF count.
    if (symbol_size && symbol_size / DRIVER_ELF_SYMBOL_SIZE < UINT32_MAX && !version_symbols_invalid &&
        (!version_symbols_present || (version_symbol_size % sizeof(u16) == 0 &&
                                     version_symbol_size / sizeof(u16) >= symbol_size / DRIVER_ELF_SYMBOL_SIZE)))
    {
        u64 symbol_count = symbol_size / DRIVER_ELF_SYMBOL_SIZE;
        bool versioned = version_symbol_size / sizeof(u16) >= symbol_count;
        library->exported_data_symbols = collect_data ? arena_allocate(arena, NativeDynamicDataSymbol, symbol_count) : 0;
        library->versioned_symbols = arena_allocate(arena, NativeDynamicVersionedSymbol, symbol_count);
        library->referenced_symbols = arena_allocate(arena, String8, symbol_count);
        for (u64 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
        {
            u64 symbol = symbol_offset + symbol_index * DRIVER_ELF_SYMBOL_SIZE;
            u32 name = 0;
            u16 section = 0;
            u64 value = 0;
            u64 size = 0;
            if (!compiler_driver_read_u32(bytes, symbol, &name) || !compiler_driver_read_u16(bytes, symbol + 6, &section) ||
                !compiler_driver_read_u64(bytes, symbol + 8, &value) || !compiler_driver_read_u64(bytes, symbol + 16, &size))
            {
                continue;
            }
            u8 info = bytes.pointer[symbol + 4];
            u8 visibility = (u8)(bytes.pointer[symbol + 5] & 3);
            u8 binding = (u8)(info >> 4);
            // Global or weak entries only: a local one names nothing this
            // executable could bind to or define for the library.
            if ((binding != 1 && binding != 2) || !name || name >= string_size)
            {
                continue;
            }
            u64 length = 0;
            while ((u64)name + length < string_size && bytes.pointer[string_offset + name + length])
            {
                length += 1;
            }
            if (!length || (u64)name + length >= string_size)
            {
                continue;
            }
            String8 spelling = {.pointer = (char8*)bytes.pointer + string_offset + name, .length = length};
            // An undefined entry is a name the library expects some other
            // module -- possibly this executable -- to define.
            if (!section)
            {
                library->referenced_symbols[library->referenced_symbol_count++] = spelling;
                continue;
            }
            u16 version = 0;
            if (versioned)
            {
                compiler_driver_read_u16(bytes, version_symbol_offset + symbol_index * sizeof(u16), &version);
            }
            u32 version_index = version & DRIVER_ELF_VERSION_INDEX_MAX;
            library->versioned_symbols[library->versioned_symbol_count++] = (NativeDynamicVersionedSymbol){
                .name = spelling,
                // VER_NDX_LOCAL and VER_NDX_GLOBAL name no version, so a
                // reference to such a definition records none either.
                .version = version_index > 1 && version_index < version_name_count ? version_names[version_index] : (String8){0},
                .has_default = (version & DRIVER_ELF_VERSION_HIDDEN) == 0 && (visibility == 0 || visibility == 3),
                .elf_type = (u8)(info & 0xf),
                .elf_visibility = visibility,
            };
            // STT_OBJECT is what a copy relocation applies to, and an absolute
            // or address-less entry names no storage to copy.
            if (collect_data && (info & 0xf) == DRIVER_ELF_SYMBOL_TYPE_OBJECT && section != DRIVER_ELF_SECTION_ABSOLUTE && value)
            {
                library->exported_data_symbols[library->exported_data_symbol_count++] = (NativeDynamicDataSymbol){
                    .name = spelling,
                    .address = value,
                    .size = size,
                };
            }
        }
        // Every name this library defines is now recorded, so the linker may
        // read the absence of a name from `versioned_symbols` as the library
        // not defining it -- which is what makes an undefined weak reference
        // to a name nothing exports resolve to zero rather than becoming an
        // import.  The ELF export list is `versioned_symbols` itself rather
        // than a second copy of the same names in `exported_symbols`, which
        // stays the PE side's.
        library->exports_known = true;
        result = true;
    }

    return result;
}

// Both ELF library searches use target roots after explicit -L directories.
// A sysroot replaces every default host root. Without one, /usr/<triple>/lib
// also covers Debian's cross-libc/compiler-runtime installation convention.
BUSTER_GLOBAL_LOCAL u32 compiler_driver_elf_library_roots(Arena* arena, CompilerDriverInvocation invocation, String8* roots)
{
    String8 multiarch = invocation.target.cpu_arch == CPU_ARCH_AARCH64 ? S8("aarch64-linux-gnu") : S8("x86_64-linux-gnu");
    u32 root_count = 0;
    if (invocation.sysroot.length)
    {
        roots[root_count++] = string_format(arena, S8("{S8}/lib/{S8}"), invocation.sysroot, multiarch);
        roots[root_count++] = string_format(arena, S8("{S8}/usr/lib/{S8}"), invocation.sysroot, multiarch);
        roots[root_count++] = string_format(arena, S8("{S8}/lib64"), invocation.sysroot);
        roots[root_count++] = string_format(arena, S8("{S8}/usr/lib64"), invocation.sysroot);
        roots[root_count++] = string_format(arena, S8("{S8}/lib"), invocation.sysroot);
        roots[root_count++] = string_format(arena, S8("{S8}/usr/lib"), invocation.sysroot);
    }
    else
    {
        roots[root_count++] = string_format(arena, S8("/lib/{S8}"), multiarch);
        roots[root_count++] = string_format(arena, S8("/usr/lib/{S8}"), multiarch);
        roots[root_count++] = string_format(arena, S8("/usr/{S8}/lib"), multiarch);
        roots[root_count++] = S8("/lib64");
        roots[root_count++] = S8("/usr/lib64");
        roots[root_count++] = S8("/lib");
        roots[root_count++] = S8("/usr/lib");
    }
    return root_count;
}

// GNU ld scripts use C comments as whitespace. Only the initial command is
// recognized here: the driver diagnoses unsupported input, never evaluates it.
BUSTER_GLOBAL_LOCAL u64 compiler_driver_linker_script_skip_trivia(ByteSlice bytes, u64 cursor)
{
    bool scanning = true;
    while (scanning && cursor < bytes.length)
    {
        u8 byte = bytes.pointer[cursor];
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\v' || byte == '\f')
        {
            cursor += 1;
        }
        else if (byte == '/' && bytes.length - cursor >= 2 && bytes.pointer[cursor + 1] == '*')
        {
            cursor += 2;
            while (bytes.length - cursor >= 2 && !(bytes.pointer[cursor] == '*' && bytes.pointer[cursor + 1] == '/'))
            {
                cursor += 1;
            }
            cursor = bytes.length - cursor >= 2 ? cursor + 2 : bytes.length;
        }
        else
        {
            scanning = false;
        }
    }
    return cursor;
}

bool compiler_driver_elf_linker_script(ByteSlice bytes)
{
    static String8 const commands[] = {
        S8_INITIALIZER("INPUT"), S8_INITIALIZER("GROUP"), S8_INITIALIZER("AS_NEEDED"),
        S8_INITIALIZER("OUTPUT_FORMAT"), S8_INITIALIZER("OUTPUT_ARCH"), S8_INITIALIZER("SEARCH_DIR"),
    };
    bool result = false;
    if (bytes.pointer)
    {
        u64 cursor = compiler_driver_linker_script_skip_trivia(bytes, 0);
        u64 start = cursor;
        while (cursor < bytes.length && ((bytes.pointer[cursor] >= 'A' && bytes.pointer[cursor] <= 'Z') || bytes.pointer[cursor] == '_'))
        {
            cursor += 1;
        }
        String8 command = {.pointer = (char8*)bytes.pointer + start, .length = cursor - start};
        bool recognized = false;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(commands); index += 1)
        {
            recognized |= string_equal(command, commands[index]);
        }
        cursor = compiler_driver_linker_script_skip_trivia(bytes, cursor);
        result = recognized && cursor < bytes.length && bytes.pointer[cursor] == '(';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_elf_library_exports(Arena* arena, CompilerDriverInvocation invocation, bool collect_data,
                                                             NativeDynamicLibrary* library, FileMapRead* export_map, String8* script_path)
{
    u16 machine = invocation.target.cpu_arch == CPU_ARCH_AARCH64 ? 183 : 62;
    String8 roots[7];
    u32 root_count = compiler_driver_elf_library_roots(arena, invocation, roots);
    *export_map = (FileMapRead){0};
    bool found = false;
    bool script_found = false;
    u32 candidate_count = invocation.library_path_count + root_count + 1;
    for (u32 path_index = 0; !found && !script_found && path_index < candidate_count; path_index += 1)
    {
        // Every candidate is zero-terminated: os_file_open takes the path as a
        // C string, and the bare library name is one this driver built with a
        // length and no terminator of its own.
        String8 path = path_index < invocation.library_path_count
                           ? string_format_z(arena, S8("{S8}/{S8}"), invocation.library_paths[path_index], library->name)
                       : path_index < invocation.library_path_count + root_count
                           ? string_format_z(arena, S8("{S8}/{S8}"), roots[path_index - invocation.library_path_count], library->name)
                           : string_duplicate_arena(arena, library->name, true);
        FileMapRead file = file_map_read(arena, path, (FileReadOptions){0});
        found = file.bytes.pointer && compiler_driver_elf_dynamic_symbols(arena, file.bytes, machine, collect_data, library);
        script_found = script_path && !found && compiler_driver_elf_linker_script(file.bytes);
        if (script_found && script_path)
        {
            *script_path = path;
        }
        if (found)
        {
            *export_map = file;
        }
        else
        {
            library->exports_known = false;
            library->exported_data_symbols = 0;
            library->exported_data_symbol_count = 0;
            library->versioned_symbols = 0;
            library->versioned_symbol_count = 0;
            library->referenced_symbols = 0;
            library->referenced_symbol_count = 0;
            file_map_unmap(file);
        }
    }
}

BUSTER_GLOBAL_LOCAL void compiler_driver_dynamic_libraries_release(CompilerDriverDynamicLibraries* libraries)
{
    for (u32 index = 0; index < libraries->export_map_count; index += 1)
    {
        file_map_unmap(libraries->export_maps[index]);
    }
}

// Whether this link has any undefined data symbol at all.  Only such a link
// needs a shared library's symbol table read, and that read is the one part of
// building the dynamic library list that touches the file system.
BUSTER_GLOBAL_LOCAL bool compiler_driver_object_imports_data(ObjectFile* object)
{
    bool result = false;
    for (u32 index = 0; !result && index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = object->symbols + index;
        result = symbol->global && symbol->section == OBJECT_SECTION_UNDEFINED && symbol->kind == OBJECT_SYMBOL_DATA;
    }

    return result;
}

// Exact compiler ABI entry points emitted by the native half/quad lowering.
// Ordinary undefined names and weak optional references do not add a runtime.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_compiler_runtime_symbol(String8 name)
{
    static String8 const names[] = {
        S8_INITIALIZER("__truncsfhf2"), S8_INITIALIZER("__truncdfhf2"), S8_INITIALIZER("__truncxfhf2"), S8_INITIALIZER("__extendhfsf2"),
        S8_INITIALIZER("__addtf3"), S8_INITIALIZER("__subtf3"), S8_INITIALIZER("__multf3"), S8_INITIALIZER("__divtf3"),
        S8_INITIALIZER("__eqtf2"), S8_INITIALIZER("__netf2"), S8_INITIALIZER("__lttf2"), S8_INITIALIZER("__letf2"),
        S8_INITIALIZER("__gttf2"), S8_INITIALIZER("__getf2"),
        S8_INITIALIZER("__trunctfhf2"), S8_INITIALIZER("__trunctfsf2"), S8_INITIALIZER("__trunctfdf2"),
        S8_INITIALIZER("__extendhftf2"), S8_INITIALIZER("__extendsftf2"), S8_INITIALIZER("__extenddftf2"),
        S8_INITIALIZER("__fixtfsi"), S8_INITIALIZER("__fixtfdi"), S8_INITIALIZER("__fixtfti"),
        S8_INITIALIZER("__fixunstfsi"), S8_INITIALIZER("__fixunstfdi"), S8_INITIALIZER("__fixunstfti"),
        S8_INITIALIZER("__floatsitf"), S8_INITIALIZER("__floatditf"), S8_INITIALIZER("__floattitf"),
        S8_INITIALIZER("__floatunsitf"), S8_INITIALIZER("__floatunditf"), S8_INITIALIZER("__floatuntitf"),
    };
    bool result = false;
    if (name.length > 2 && name.pointer[0] == '_' && name.pointer[1] == '_')
    {
        for (u32 index = 0; !result && index < BUSTER_ARRAY_LENGTH(names); index += 1)
        {
            result = string_equal(name, names[index]);
        }
    }
    return result;
}

// 0 means absent, 1 callable, 2 a default data/unknown definition that would
// preempt an appended runtime. Do not silently call that definition as code.
BUSTER_GLOBAL_LOCAL u32 compiler_driver_elf_library_helper_provider(NativeDynamicLibrary const* library, String8 name)
{
    enum { ELF_SYMBOL_FUNCTION = 2, ELF_SYMBOL_IFUNC = 10 };
    u32 result = 0;
    for (u32 index = 0; !result && index < library->versioned_symbol_count; index += 1)
    {
        NativeDynamicVersionedSymbol const* symbol = library->versioned_symbols + index;
        if (symbol->has_default && (symbol->elf_visibility == 0 || symbol->elf_visibility == 3) && string_equal(symbol->name, name))
        {
            result = symbol->elf_type == ELF_SYMBOL_FUNCTION || symbol->elf_type == ELF_SYMBOL_IFUNC ? 1u : 2u;
        }
    }
    return result;
}

// Explicit DSOs retain precedence, and a definition selected from an object or
// archive has already disappeared from the undefined set. Missing target libc
// must not turn a known missing compiler helper into a blind loader import.
BUSTER_GLOBAL_LOCAL void compiler_driver_elf_compiler_runtime(Arena* arena, CompilerDriverInvocation invocation,
                                                             ObjectFile const* linked, bool imports_data,
                                                             CompilerDriverDynamicLibraries* libraries)
{
    u32 runtime_index = UINT32_MAX;
    for (u32 index = 0; index < libraries->count; index += 1)
    {
        if (string_equal(libraries->pointer[index].name, S8("libgcc_s.so.1")))
        {
            runtime_index = index;
        }
    }
    for (u32 index = 0; !libraries->missing_runtime_symbol.length && index < linked->symbol_count; index += 1)
    {
        ObjectSymbol const* symbol = linked->symbols + index;
        if (symbol->global && !symbol->weak && !symbol->hidden && symbol->section == OBJECT_SECTION_UNDEFINED &&
            symbol->kind == OBJECT_SYMBOL_FUNCTION &&
            compiler_driver_elf_compiler_runtime_symbol(symbol->name))
        {
            NativeDynamicLibrary const* provider = &libraries->runtime;
            u32 provided = compiler_driver_elf_library_helper_provider(provider, symbol->name);
            for (u32 library_index = 0; !provided && library_index < libraries->count; library_index += 1)
            {
                provider = libraries->pointer + library_index;
                provided = compiler_driver_elf_library_helper_provider(provider, symbol->name);
            }
            if (!provided && runtime_index == UINT32_MAX)
            {
                runtime_index = libraries->count++;
                NativeDynamicLibrary* runtime = libraries->pointer + runtime_index;
                *runtime = (NativeDynamicLibrary){.name = S8("libgcc_s.so.1")};
                FileMapRead* export_map = libraries->export_maps + libraries->export_map_count;
                compiler_driver_elf_library_exports(arena, invocation, imports_data, runtime, export_map, 0);
                libraries->export_map_count += export_map->bytes.pointer != 0;
                provider = runtime;
                provided = compiler_driver_elf_library_helper_provider(runtime, symbol->name);
            }
            if (provided != 1)
            {
                libraries->missing_runtime_symbol = symbol->name;
                libraries->runtime_failure_library = provided == 2 ? provider->name : S8("libgcc_s.so.1");
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL CompilerDriverDynamicLibraries compiler_driver_dynamic_libraries(Arena* arena, CompilerDriverInvocation invocation, bool* static_libraries,
                                                                                    bool imports_data, ObjectFile const* linked)
{
    CompilerDriverDynamicLibraries result = {0};
    static String8 const windows_system_libraries[] = {
        S8_INITIALIZER("kernel32.dll"),
        S8_INITIALIZER("user32.dll"),
        S8_INITIALIZER("gdi32.dll"),
        S8_INITIALIZER("ws2_32.dll"),
        S8_INITIALIZER("dwmapi.dll"),
        S8_INITIALIZER("shell32.dll"),
        S8_INITIALIZER("vcruntime140.dll"),
    };
    u32 default_library_count = invocation.target.os == OPERATING_SYSTEM_WINDOWS ? BUSTER_ARRAY_LENGTH(windows_system_libraries) : 0;
    u32 library_capacity = invocation.library_count + invocation.framework_count + default_library_count +
                           (invocation.target.os == OPERATING_SYSTEM_LINUX ? 1u : 0u);
    NativeDynamicLibrary* libraries =
        arena_allocate(arena, NativeDynamicLibrary, library_capacity);
    // The `-l` spelling that produced each entry, kept beside the mapped file
    // name so a library the search below never finds is reported as the
    // request the caller wrote rather than as the soname it was mapped to.
    // Entries the caller did not request -- the Windows defaults and the
    // Apple frameworks -- keep an empty request and are never reported.
    String8* requests = arena_allocate(arena, String8, library_capacity);
    memset(requests, 0, sizeof(*requests) * library_capacity);
    u32 count = 0;
    for (u32 index = 0; index < default_library_count; index += 1)
    {
        libraries[count++] = (NativeDynamicLibrary){
            .name = windows_system_libraries[index],
        };
    }
    for (u32 index = 0; index < invocation.library_count; index += 1)
    {
        if (static_libraries && static_libraries[index])
        {
            continue;
        }
        String8 requested = invocation.libraries[index];
        if (!requested.length || string_equal(requested, S8("c")))
        {
            continue;
        }
        String8 name = {0};
        if (requested.pointer[0] == ':')
        {
            name = (String8){
                .pointer = requested.pointer + 1,
                .length = requested.length - 1,
            };
        }
        else if (invocation.target.os == OPERATING_SYSTEM_ANDROID)
        {
            name = string_format(arena, S8("lib{S8}.so"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_LINUX)
        {
            name = string_equal(requested, S8("m"))         ? S8("libm.so.6")
                   : string_equal(requested, S8("pthread")) ? S8("libpthread.so.0")
                   : string_equal(requested, S8("dl"))      ? S8("libdl.so.2")
                   : string_equal(requested, S8("rt"))      ? S8("librt.so.1")
                                                            : string_format(arena, S8("lib{S8}.so"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS)
        {
            if (string_equal(requested, S8("m")))
            {
                continue;
            }
            name = string_format(arena, S8("/usr/lib/lib{S8}.dylib"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
        {
            bool has_dll_suffix = requested.length >= 4 && string_equal(
                                                               (String8){
                                                                   .pointer = requested.pointer + requested.length - 4,
                                                                   .length = 4,
                                                               },
                                                               S8(".dll"));
            name = has_dll_suffix ? requested : string_format(arena, S8("{S8}.dll"), requested);
        }
        else
        {
            name = requested;
        }
        if (!name.length)
        {
            continue;
        }
        bool duplicate = false;
        for (u32 previous = 0; previous < count; previous += 1)
        {
            duplicate |= string_equal(libraries[previous].name, name);
        }
        if (!duplicate)
        {
            requests[count] = requested;
            libraries[count++] = (NativeDynamicLibrary){
                .name = name,
            };
        }
    }
    if (invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS)
    {
        for (u32 index = 0; index < invocation.framework_count; index += 1)
        {
            String8 framework = invocation.frameworks[index];
            if (!framework.length)
            {
                continue;
            }
            String8 root = invocation.framework_path_count ? invocation.framework_paths[0] : S8("/System/Library/Frameworks");
            String8 name = string_format(arena, S8("{S8}/{S8}.framework/{S8}"), root, framework, framework);
            bool duplicate = false;
            for (u32 previous = 0; previous < count; previous += 1)
            {
                duplicate |= string_equal(libraries[previous].name, name);
            }
            if (!duplicate)
            {
                libraries[count++] = (NativeDynamicLibrary){
                    .name = name,
                };
            }
        }
    }
    if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        result.export_maps = arena_allocate(arena, FileMapRead, count + 1);
        result.runtime.name = S8("ucrtbase.dll");
        FileMapRead* export_map = result.export_maps + result.export_map_count;
        compiler_driver_pe_library_exports(arena, invocation, &result.runtime, export_map);
        result.export_map_count += export_map->bytes.pointer != 0;
        for (u32 index = 0; index < count; index += 1)
        {
            export_map = result.export_maps + result.export_map_count;
            compiler_driver_pe_library_exports(arena, invocation, &libraries[index], export_map);
            result.export_map_count += export_map->bytes.pointer != 0;
        }
    }
    else if (invocation.target.os == OPERATING_SYSTEM_LINUX)
    {
        // libc.so.6 is the library the ELF writers name themselves, so it is
        // read as the runtime rather than as one of the requested ones.
        //
        // Every hosted ELF link reads these, not only one that imports data:
        // symbol versions apply to functions too, and a reference that binds
        // to a version has to record it.  The data objects -- the half that
        // needs an address and a size -- stay behind imports_data, since a
        // link with no undefined data symbol has nothing to copy.  Reading
        // libc.so.6 where nothing did before costs about 0,65 M instructions
        // on this host, a tenth of a percent of the smallest hosted compile.
        result.export_maps = arena_allocate(arena, FileMapRead, count + 2);
        result.runtime.name = S8("libc.so.6");
        FileMapRead* export_map = result.export_maps + result.export_map_count;
        compiler_driver_elf_library_exports(arena, invocation, imports_data, &result.runtime, export_map, 0);
        result.export_map_count += export_map->bytes.pointer != 0;
        for (u32 index = 0; index < count; index += 1)
        {
            export_map = result.export_maps + result.export_map_count;
            String8 script_path = {0};
            compiler_driver_elf_library_exports(arena, invocation, imports_data, &libraries[index], export_map, &script_path);
            result.export_map_count += export_map->bytes.pointer != 0;
            // The scan walked every search directory the loader would, so a
            // library it did not find is one the produced executable could
            // never load -- and one the archive search above did not satisfy
            // statically either, or the entry would not be here.  Record the
            // first such request for the caller to refuse the link with.
            if (!libraries[index].exports_known && requests[index].length && !result.missing_request.length)
            {
                result.missing_request = requests[index];
                result.unsupported_script_path = script_path;
            }
        }
    }
    result.pointer = libraries;
    result.count = count;
    if (invocation.target.os == OPERATING_SYSTEM_LINUX)
    {
        compiler_driver_elf_compiler_runtime(arena, invocation, linked, imports_data, &result);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverDynamicLibraries compiler_driver_target_dynamic_libraries(Arena* arena, CompilerDriverInvocation invocation,
                                                                                              bool* static_libraries, ObjectFile* linked)
{
    CompilerDriverDynamicLibraries result;
    if (invocation.target.os == OPERATING_SYSTEM_UEFI)
    {
        result = (CompilerDriverDynamicLibraries){0};
    }
    else
    {
        result = compiler_driver_dynamic_libraries(arena, invocation, static_libraries, compiler_driver_object_imports_data(linked), linked);
    }

    return result;
}

// Legacy API-built invocations still carry separate raw `-D` operands. Split
// those at their first `=` before handing them to CPreprocessOptions; parsed
// command lines keep their raw operands in the authoritative ordered stream.
BUSTER_GLOBAL_LOCAL CPreprocessorDefinition compiler_driver_c_definition(String8 definition)
{
    for (u64 index = 0; index < definition.length; index += 1)
    {
        if (definition.pointer[index] == '=')
        {
            return (CPreprocessorDefinition){
                .name =
                    {
                        .pointer = definition.pointer,
                        .length = index,
                    },
                .value =
                    {
                        .pointer = definition.pointer + index + 1,
                        .length = definition.length - index - 1,
                    },
            };
        }
    }
    return (CPreprocessorDefinition){
        .name = definition,
        .value = S8("1"),
    };
}

// An ELF64 shared header with a foreign machine cannot satisfy this target.
// Files without that recognized header still enter export discovery; this
// header discrimination is not full validation of an alien shared object.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_shared_is_incompatible(ByteSlice bytes, Target target)
{
    u16 type = 0;
    u16 machine = 0;
    bool recognized = target.os == OPERATING_SYSTEM_LINUX && bytes.length >= 64 &&
                      memcmp(bytes.pointer, "\x7f" "ELF", 4) == 0 && bytes.pointer[4] == 2 && bytes.pointer[5] == 1 && bytes.pointer[6] == 1 &&
                      compiler_driver_read_u16(bytes, 16, &type) && type == 3 && compiler_driver_read_u16(bytes, 18, &machine);
    u16 expected = target.cpu_arch == CPU_ARCH_AARCH64 ? 183 : 62;
    return recognized && machine != expected;
}

BUSTER_GLOBAL_LOCAL ObjectArchive compiler_driver_library_archive(Arena* arena, CompilerDriverInvocation invocation, String8 requested, bool* found,
                                                                  String8* path_out, FileMapRead* map_out)
{
    ObjectArchive result = {0};
    bool exact = requested.length && requested.pointer[0] == ':';
    String8 exact_name = exact ?
        (String8){
            .pointer = requested.pointer + 1,
            .length = requested.length - 1,
        } :
        (String8){0};
    bool exact_archive = exact && compiler_driver_archive_input(exact_name);
    String8 roots[7];
    u32 root_count = invocation.target.os == OPERATING_SYSTEM_LINUX ? compiler_driver_elf_library_roots(arena, invocation, roots) : 0;
    u32 candidate_count = invocation.library_path_count + root_count;
    for (u32 path_index = 0; path_index < candidate_count; path_index += 1)
    {
        String8 root = path_index < invocation.library_path_count ? invocation.library_paths[path_index]
                                                                : roots[path_index - invocation.library_path_count];
        if (!exact_archive && invocation.target.os != OPERATING_SYSTEM_UEFI)
        {
            String8 shared_name = invocation.target.os == OPERATING_SYSTEM_WINDOWS ? string_format(arena, S8("{S8}.dll"), requested)
                                  : invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS
                                      ? string_format(arena, S8("lib{S8}.dylib"), requested)
                                      : string_format(arena, S8("lib{S8}.so"), requested);
            String8 shared_path = string_format_z(arena, S8("{S8}/{S8}"), root, shared_name);
            FileMapRead shared_map = file_map_read(arena, shared_path, (FileReadOptions){0});
            ByteSlice shared = shared_map.bytes;
            if (shared.pointer && !compiler_driver_elf_shared_is_incompatible(shared, invocation.target))
            {
                file_map_unmap(shared_map);
                return result;
            }
            file_map_unmap(shared_map);
        }
        String8 archive_name = exact_archive                                      ? exact_name
                               : invocation.target.os == OPERATING_SYSTEM_WINDOWS ? string_format(arena, S8("{S8}.lib"), requested)
                                                                                  : string_format(arena, S8("lib{S8}.a"), requested);
        String8 archive_path = string_format_z(arena, S8("{S8}/{S8}"), root, archive_name);
        FileMapRead archive_map = file_map_read(arena, archive_path, (FileReadOptions){0});
        ByteSlice archive_bytes = archive_map.bytes;
        if (!archive_bytes.pointer)
        {
            file_map_unmap(archive_map);
            continue;
        }
        *found = true;
        *path_out = archive_path;
        ObjectArchive archive = object_archive_read_link(arena, archive_bytes, invocation.target);
        *map_out = archive_map;
        return archive;
    }
    if (exact_archive)
    {
        FileMapRead archive_map = file_map_read(arena, exact_name, (FileReadOptions){0});
        ByteSlice archive_bytes = archive_map.bytes;
        if (archive_bytes.pointer)
        {
            *found = true;
            *path_out = exact_name;
            ObjectArchive archive = object_archive_read_link(arena, archive_bytes, invocation.target);
            *map_out = archive_map;
            return archive;
        }
        file_map_unmap(archive_map);
    }
    return result;
}

// The -E text is read by other programs, not only by people: autoconf's
// established idiom preprocesses a file of literal lines and greps the output
// for one of them -- CPython's Misc/platform_triplet.c is
// `grep '^PLATFORM_TRIPLET='` -- so tokens that shared a source line must
// share an output line, and tokens that touched in the source should touch in
// the output when their emitted spellings remain lexically separate. A printer
// that space-joined the whole stream onto one line made that grep read the
// triplet as empty. Both facts are recovered from the source map: a line gap
// becomes a newline (capped, since without line markers a skipped
// conditional's size is not information), and adjacency is the previous
// token's column plus its emitted width reaching the next token's column.
// Respelling and replacement can make those different coordinate systems
// coincide accidentally, so c_token_requires_separator (shared with the
// frontend's source-quoting diagnostics) protects every apparent adjacency.
// Tokens from expanded lines retain boundary whitespace in an optional cold
// sidecar: invocation columns cannot recover adjacency after replacement
// changes spelling width. The lexical separator still protects every pair.
// Results without this sidecar retain column recovery, including hand-built
// results with no recovery map, which degrade to a single space.
//
// The effective #pragma pack state is also a fact the stream carries: the
// preprocessor consumes every pragma, so a later compile of this text would
// lay records out naturally. With emit_pack_pragmas, a `#pragma pack(N)` (or
// `#pragma pack()` for natural alignment) line is written before the first
// token under each recorded pack change, mirroring GCC and Clang, whose -E
// output keeps the pragma. Assembly sources pass false: the text feeds an
// assembler, where the line would be a syntax error.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_preprocess_text(Arena* arena, CPreprocessResult preprocess, u64 lookup_offset, CSourceLocation* lookup,
                                                            bool emit_pack_pragmas)
{
    enum
    {
        compiler_driver_preprocess_line_gap_cap = 8,
        compiler_driver_preprocess_pack_line_capacity = 32,
    };
    u64 capacity = 2;
    u32 pack_change_count = emit_pack_pragmas ? preprocess.pack_change_count : 0;
    capacity += (u64)pack_change_count * compiler_driver_preprocess_pack_line_capacity;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        if (preprocess.tokens[index].kind != C_TOKEN_END_OF_FILE)
        {
            capacity += c_token_length(preprocess.spelling_base, preprocess.tokens[index]) + compiler_driver_preprocess_line_gap_cap + 1;
        }
    }
    char8* text = arena_allocate(arena, char8, capacity);
    u64 length = 0;
    u32 previous_line = 1;
    u32 previous_file = 0;
    u32 previous_end_column = 0;
    CToken previous_token = {0};
    String8 previous_spelling = {0};
    u32 next_pack_change = 0;
    bool at_line_start = false;
    u8 const* output_spacing = c_preprocess_detail(preprocess)->output_spacing;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_END_OF_FILE)
        {
            break;
        }
        CSourceLocation location = c_preprocess_token_location(&preprocess, token);
        String8 spelling = c_token_spelling(preprocess.spelling_base, token);
        bool pack_pending = false;
        u32 pack_alignment = 0;
        while (next_pack_change < pack_change_count && preprocess.pack_changes[next_pack_change].token_index <= index)
        {
            pack_pending = true;
            pack_alignment = preprocess.pack_changes[next_pack_change].alignment;
            next_pack_change += 1;
        }
        if (pack_pending)
        {
            if (length)
            {
                if (previous_token.punctuator == C_PUNCTUATOR_BACKSLASH)
                {
                    text[length++] = ' ';
                }
                text[length++] = '\n';
            }
            memcpy(text + length, "#pragma pack(", 13);
            length += 13;
            if (pack_alignment)
            {
                char8 digits[10];
                u32 digit_count = 0;
                for (u32 rest = pack_alignment; rest; rest /= 10)
                {
                    digits[digit_count++] = (char8)('0' + rest % 10);
                }
                while (digit_count)
                {
                    text[length++] = digits[--digit_count];
                }
            }
            text[length++] = ')';
            text[length++] = '\n';
            at_line_start = true;
        }
        if (length && !at_line_start)
        {
            if (location.file != previous_file || location.line < previous_line)
            {
                if (previous_token.punctuator == C_PUNCTUATOR_BACKSLASH)
                {
                    text[length++] = ' ';
                }
                text[length++] = '\n';
            }
            else if (location.line > previous_line)
            {
                if (previous_token.punctuator == C_PUNCTUATOR_BACKSLASH)
                {
                    text[length++] = ' ';
                }
                u32 gap = location.line - previous_line;
                gap = gap > compiler_driver_preprocess_line_gap_cap ? compiler_driver_preprocess_line_gap_cap : gap;
                for (u32 newline = 0; newline < gap; newline += 1)
                {
                    text[length++] = '\n';
                }
            }
            else if ((output_spacing && output_spacing[index] ? output_spacing[index] == C_OUTPUT_SPACING_SEPARATED : location.column != previous_end_column) ||
                     c_token_requires_separator(previous_token, previous_spelling, token, spelling))
            {
                text[length++] = ' ';
            }
        }
        if (lookup && lookup_offset >= length && lookup_offset - length < spelling.length)
        {
            *lookup = location;
        }
        memcpy(text + length, spelling.pointer, spelling.length);
        length += spelling.length;
        at_line_start = false;
        previous_line = location.line;
        previous_file = location.file;
        previous_end_column = location.column + (u32)spelling.length;
        previous_token = token;
        previous_spelling = spelling;
    }
    if (previous_token.punctuator == C_PUNCTUATOR_BACKSLASH)
    {
        text[length++] = ' ';
    }
    text[length++] = '\n';
    text[length] = 0;
    return (String8){
        .pointer = text,
        .length = length,
    };
}

typedef struct CompilerDriverWarningChunk CompilerDriverWarningChunk;
struct CompilerDriverWarningChunk
{
    CompilerDriverWarningChunk* next;
    String8 text;
};

typedef struct CompilerDriverDiagnosticCollector CompilerDriverDiagnosticCollector;
struct CompilerDriverDiagnosticCollector
{
    Arena* arena;
    CompilerDriverWarningChunk* first;
    CompilerDriverWarningChunk* last;
    u64 length;
    CompilerDiagnostic* records;
    u32 record_count;
    u32 record_capacity;
    bool suppress_records;
    // -w: warnings are neither recorded nor rendered.
    bool suppress_warnings;
};

#include <buster/lib/compiler/driver/driver_diagnostic.c>

BUSTER_GLOBAL_LOCAL void compiler_driver_warning_append_text(CompilerDriverDiagnosticCollector* collector, String8 text)
{
    if (!collector || !collector->arena || !text.length)
    {
        return;
    }
    CompilerDriverWarningChunk* chunk = arena_allocate(collector->arena, CompilerDriverWarningChunk, 1);
    *chunk = (CompilerDriverWarningChunk){
        .text = text,
    };
    if (collector->last)
    {
        collector->last->next = chunk;
    }
    else
    {
        collector->first = chunk;
    }
    collector->last = chunk;
    collector->length += text.length;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_warning_flatten(CompilerDriverDiagnosticCollector collector)
{
    if (!collector.arena || !collector.length)
    {
        return (String8){0};
    }
    char8* text = arena_allocate(collector.arena, char8, collector.length + 1);
    u64 offset = 0;
    for (CompilerDriverWarningChunk* chunk = collector.first; chunk; chunk = chunk->next)
    {
        memcpy(text + offset, chunk->text.pointer, chunk->text.length);
        offset += chunk->text.length;
    }
    text[offset] = 0;
    return (String8){
        .pointer = text,
        .length = offset,
    };
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_publish_c_diagnostics(Arena* arena, CompilerDriverDiagnosticCollector* collector,
                                                                   CPreprocessResult const* preprocess, CDiagnostic* diagnostics, u64 count, String8 path, String8 split_source)
{
    String8 first_error = {0};
    for (u64 index = 0; index < count; index += 1)
    {
        CompilerDiagnostic diagnostic = compiler_driver_c_diagnostic(preprocess, diagnostics[index], path);
        if (split_source.length) diagnostic.primary = compiler_driver_assembly_unsplit_location(split_source, path, diagnostic.primary);
        bool warning = diagnostic.severity == COMPILER_DIAGNOSTIC_WARNING;
        if (!warning || !collector->suppress_warnings)
        {
            compiler_driver_collect_diagnostic(collector, diagnostic);
        }
        if (warning)
        {
            if (!collector->suppress_warnings)
            {
                String8 rendered = compiler_diagnostic_render(collector->arena, diagnostic);
                compiler_driver_warning_append_text(collector, rendered);
                compiler_driver_warning_append_text(collector, S8("\n"));
            }
        }
        else if (!first_error.length)
        {
            first_error = compiler_diagnostic_render(arena, diagnostic);
        }
    }
    return first_error;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_object_path(Arena* arena, String8 input);

// Per-unit measurement scratch for collect_input_metrics. A null pointer is
// the ordinary compile: every phase boundary tests it once and reads no
// clock. Only scalars live here, so a lane can fill it in its own slot and
// the caller copies it after the gang joins. Every boundary is an offset from
// the invocation's metrics origin, so the phases telescope to exactly
// end_nanoseconds - start_nanoseconds whatever the clock's tick conversion.
typedef struct CompilerDriverUnitMetrics CompilerDriverUnitMetrics;
struct CompilerDriverUnitMetrics
{
    TimeDataType origin;
    u64 start_nanoseconds;
    u64 last_nanoseconds;
    u64 phase_nanoseconds[COMPILER_DRIVER_PHASE_COUNT];
    u64 object_file_bytes;
    u64 arena_peak_bytes;
    u64 arena_retained_bytes;
    // The unit's defined functions are the FUNCTION symbols among the first
    // function_symbol_limit object symbols: canonical codegen writes one per
    // entry before its data and alias symbols, an assembly unit's are
    // scattered through its whole table.
    u32 function_symbol_limit;
    CompilerDriverPhase active;
};

// Credits the time since the previous boundary to the phase that was
// running and makes `phase` the running one.
BUSTER_GLOBAL_LOCAL void compiler_driver_phase_begin(CompilerDriverUnitMetrics* metrics, CompilerDriverPhase phase)
{
    if (metrics)
    {
        u64 now = timestamp_ns_between(metrics->origin, timestamp_take());
        metrics->phase_nanoseconds[metrics->active] += now - metrics->last_nanoseconds;
        metrics->last_nanoseconds = now;
        metrics->active = phase;
    }
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_llvm_target_triple(Target target)
{
    if (target.cpu_arch == CPU_ARCH_WASM32)
    {
        return (String8){0};
    }
    if (target.cpu_arch == CPU_ARCH_WASM64)
    {
        return S8("wasm64-unknown-unknown");
    }
    if (target.cpu_arch == CPU_ARCH_BPFEL)
    {
        return S8("bpfel-unknown-linux");
    }
    bool aarch64 = target.cpu_arch == CPU_ARCH_AARCH64;
    if (aarch64 || target.cpu_arch == CPU_ARCH_X86_64)
    {
        switch (target.os)
        {
        case OPERATING_SYSTEM_LINUX:
            return aarch64 ? S8("aarch64-unknown-linux-gnu") : S8("x86_64-unknown-linux-gnu");
        case OPERATING_SYSTEM_ANDROID:
            return aarch64 ? S8("aarch64-unknown-linux-android") : S8("x86_64-unknown-linux-android");
        case OPERATING_SYSTEM_MACOS:
            return aarch64 ? S8("arm64-apple-macosx") : S8("x86_64-apple-macosx");
        case OPERATING_SYSTEM_IOS:
            return aarch64 ? S8("arm64-apple-ios") : S8("x86_64-apple-ios-simulator");
        case OPERATING_SYSTEM_WINDOWS:
            return aarch64 ? S8("aarch64-pc-windows-msvc") : S8("x86_64-pc-windows-msvc");
        case OPERATING_SYSTEM_UEFI:
            // The invocation boundary rejects AArch64 UEFI bitcode. Do not
            // publish a Windows triple for the distinct native AAPCS64 ABI.
            return aarch64 ? (String8){0} : S8("x86_64-unknown-windows");
        case OPERATING_SYSTEM_FREESTANDING:
            return aarch64 ? S8("aarch64-unknown-none") : S8("x86_64-unknown-none");
        case OPERATING_SYSTEM_WASI:
        case OPERATING_SYSTEM_COUNT:
            break;
        }
    }

    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_llvm_data_layout(Target target)
{
    switch (target.cpu_arch)
    {
    case CPU_ARCH_X86_64:
        if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
        {
            return S8("e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
        }
        if (target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI)
        {
            return S8("e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
        }
        return S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    case CPU_ARCH_AARCH64:
        if (target.os == OPERATING_SYSTEM_UEFI)
        {
            return (String8){0};
        }
        if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
        {
            return S8("e-m:o-i64:64-i128:128-n32:64-S128-Fn32");
        }
        if (target.os == OPERATING_SYSTEM_WINDOWS)
        {
            return S8("e-m:w-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32");
        }
        return S8("e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32");
    case CPU_ARCH_WASM32:
        return (String8){0};
    case CPU_ARCH_WASM64:
        return S8("e-m:e-p:64:64-p10:8:8-p20:8:8-i64:64-n32:64-S128-ni:1:10:20");
    case CPU_ARCH_BPFEL:
        return S8("e-m:e-p:64:64-i64:64-i128:128-n32:64-S128");
    case CPU_ARCH_SPIRV_COMPUTE:
    case CPU_ARCH_COUNT:
        break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL LlvmBitcodeOptions compiler_driver_llvm_bitcode_options(Target target, String8 source_filename)
{
    return (LlvmBitcodeOptions){
        .target_triple = compiler_driver_llvm_target_triple(target),
        .data_layout = compiler_driver_llvm_data_layout(target),
        .source_filename = source_filename,
        .deterministic = true,
        // Both driver pipelines validate immediately before reaching the
        // emitter, so do not repeat a whole-module walk here.
        .validate_ir = false,
    };
}

// Compiler artifacts retain regular-file atomic publication. Stream destinations
// use the file layer's direct write path; every refusal names its destination.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_write_failure(Arena* arena, String8 path, FilePublishResult published)
{
    String8 reason = published.status == FILE_PUBLISH_UNSUPPORTED_DESTINATION
                         ? S8("symbolic links/reparse points, directories and non-stream special destinations are refused")
                     : published.error.v ? string_format(arena, S8("{EOs}"), published.error)
                                         : S8("incomplete output publication");
    if (published.status == FILE_PUBLISH_UNSUPPORTED_DESTINATION && published.error.v)
    {
        reason = string_format(arena, S8("{S8} ({EOs})"), reason, published.error);
    }
    return string_format(arena, S8("could not write {S8}: {S8}"), path, reason);
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_publish_slices(Arena* arena, String8 path, ByteSlice const* slices, u64 slice_count, CompilerDriverResult* result)
{
    FilePublishResult published = file_publish_slices_checked(path, slices, slice_count, (OpenPermissions){.read = 1, .write = 1});
    bool success = published.status == FILE_PUBLISH_PUBLISHED;
    if (!success)
    {
        result->error = COMPILER_DRIVER_ERROR_FILE_WRITE;
        result->diagnostic = compiler_driver_write_failure(arena, path, published);
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_publish(Arena* arena, String8 path, ByteSlice bytes, CompilerDriverResult* result)
{
    return compiler_driver_publish_slices(arena, path, &bytes, 1, result);
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_llvm_bitcode_path(Arena* arena, String8 input)
{
    u64 name = 0;
    u64 extension = input.length;
    for (u64 index = input.length; index != 0; index -= 1)
    {
        char8 byte = input.pointer[index - 1];
        if (byte == '.' && extension == input.length)
        {
            extension = index - 1;
        }
        if (byte == '/' || byte == '\\')
        {
            name = index;
            break;
        }
    }
    return string_format_z(arena, S8("{S8}.bc"), (String8){
                                                        .pointer = input.pointer + name,
                                                        .length = extension - name,
                                                    });
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_write_llvm_bitcode(Arena* arena, CompilerDriverInvocation invocation, IrProgram* program, IrModule* module,
                                                            LlvmBitcodeArtifact artifact, CompilerDriverResult* result)
{
    if (!result)
    {
        return false;
    }
    result->llvm_bitcode = artifact;
    if (!llvm_bitcode_artifact_is_valid(artifact))
    {
        result->error = COMPILER_DRIVER_ERROR_LLVM_BITCODE;
        String8 message = artifact.error.diagnostic.length ? artifact.error.diagnostic
                          : artifact.error.message.length  ? artifact.error.message
                                                           : S8("LLVM bitcode emission failed");
        result->diagnostic = compiler_driver_emitter_diagnostic(arena, program, module, artifact.error.function, artifact.error.instruction, message);
        return false;
    }
    result->has_llvm_bitcode = true;
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_llvm_bitcode_path(arena, invocation.input_paths[0]);
    if (!compiler_driver_publish(arena, output, artifact.bytes, result))
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_wasm_path(Arena* arena, String8 input)
{
    u64 extension = input.length;
    for (u64 index = input.length; index != 0; index -= 1)
    {
        char8 byte = input.pointer[index - 1];
        if (byte == '.')
        {
            extension = index - 1;
            break;
        }
        if (byte == '/' || byte == '\\')
        {
            break;
        }
    }
    return string_format_z(arena, S8("{S8}.wasm"), (String8){
                                                          .pointer = input.pointer,
                                                          .length = extension,
                                                      });
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_write_wasm(Arena* arena, CompilerDriverInvocation invocation, IrProgram* program, IrModule* module,
                                                    WasmArtifact artifact, CompilerDriverResult* result)
{
    if (!result)
    {
        return false;
    }
    result->wasm = artifact;
    result->wasm64 = artifact;
    if (artifact.error.code != WASM64_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_WASM;
        String8 message = artifact.error.diagnostic.length ? artifact.error.diagnostic
                          : artifact.error.message.length  ? artifact.error.message
                                                           : S8("WebAssembly code generation failed");
        result->diagnostic = compiler_driver_emitter_diagnostic(arena, program, module, artifact.error.function, artifact.error.instruction, message);
        return false;
    }
    result->has_wasm = true;
    result->has_wasm64 = artifact.stats.memory64;
    String8 output = invocation.output_path.length ? invocation.output_path
                     : invocation.action == COMPILER_DRIVER_ACTION_OBJECT
                         ? compiler_driver_default_wasm_path(arena, invocation.input_paths[0])
                         : S8("a.wasm");
    if (!compiler_driver_publish(arena, output, artifact.bytes, result))
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_write_spirv(Arena* arena, CompilerDriverInvocation invocation, IrProgram* program, IrModule* module,
                                                    SpirvArtifact artifact, CompilerDriverResult* result)
{
    result->spirv = artifact;
    if (!artifact.success)
    {
        result->error = COMPILER_DRIVER_ERROR_SPIRV;
        result->diagnostic = compiler_driver_emitter_diagnostic(arena, program, module, artifact.function, artifact.instruction, artifact.diagnostic);
    }
    else
    {
        String8 output = invocation.output_path.length ? invocation.output_path
                          : string_format_z(arena, S8("{S8}.spv"), invocation.input_paths[0]);
        if (compiler_driver_publish(arena, output, artifact.bytes, result))
        {
            result->has_spirv = true;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_write_ebpf(Arena* arena, CompilerDriverInvocation invocation, IrProgram* program, IrModule* module,
                                                    EbpfArtifact artifact, CompilerDriverResult* result)
{
    if (!result)
    {
        return false;
    }
    result->ebpf = artifact;
    if (artifact.error.code != EBPF_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_EBPF;
        String8 message = artifact.error.diagnostic.length ? artifact.error.diagnostic
                          : artifact.error.message.length  ? artifact.error.message
                                                           : S8("eBPF code generation failed");
        result->diagnostic = compiler_driver_emitter_diagnostic(arena, program, module, artifact.error.function, artifact.error.instruction, message);
        return false;
    }
    result->has_ebpf = true;
    String8 output = invocation.output_path.length ? invocation.output_path
                     : invocation.action == COMPILER_DRIVER_ACTION_OBJECT
                         ? compiler_driver_default_object_path(arena, invocation.input_paths[0])
                         : S8("a.o");
    if (!compiler_driver_publish(arena, output, artifact.bytes, result))
    {
        return false;
    }
    return true;
}

// A failed native link names its reason and symbol. A relocation refused in
// a position-independent image receives the PIC hint only when its refused
// address model establishes that cause. Malformed sites and TLS failures do
// not say anything about the input's code model. A
// failed artifact write names the operating-system error that refused it.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_native_link_diagnostic(Arena* arena, CompilerDriverInvocation invocation, NativeExecutableLinkResult link)
{
    String8 diagnostic;
    if (link.error == LINK_ERROR_FILE_WRITE)
    {
        String8 path = link.symbol.length ? link.symbol : invocation.output_path;
        diagnostic = compiler_driver_write_failure(arena, path, (FilePublishResult){
            .error = link.write_error,
            .status = link.write_unsupported_destination ? FILE_PUBLISH_UNSUPPORTED_DESTINATION : FILE_PUBLISH_FAILED,
        });
    }
    else
    {
        String8 hint = invocation.image_kind != NATIVE_IMAGE_EXECUTABLE && link.error == LINK_ERROR_RELOCATION &&
                       link.requires_position_independent_objects
                           ? S8(" (a position-independent image needs objects compiled with -fPIC)") : S8("");
        diagnostic = string_format(arena, S8("native C link failed with {S8}: {S8}{S8}"), link_error_name(link.error), link.symbol, hint);
    }
    return diagnostic;
}

// What a finished object becomes: textual assembly for -S, a written object
// file for -c, or a linked executable. It is shared by the C pipeline above
// and by the assembly front door below, which reach the same three outputs
// through completely different producers.
BUSTER_GLOBAL_LOCAL void compiler_driver_emit_object_output(Arena* arena, CompilerDriverInvocation invocation, ObjectFile object,
                                                             bool suppress_object_write, CompilerDriverResult* result, CompilerDriverUnitMetrics* metrics)
{
    compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_EMIT);
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result->output = object_print_assembly(arena, &object);
        if (!result->output.length)
        {
            result->error = COMPILER_DRIVER_ERROR_OBJECT;
            result->diagnostic = S8("could not format native object as textual assembly");
            return;
        }
        if (invocation.output_path.length)
        {
            compiler_driver_publish(arena, invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result->output), result);
        }
        return;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
    {
        if (suppress_object_write)
        {
            return;
        }
        // The file is the only consumer of these bytes, and the object's
        // section payloads stay live in this arena until it is written, so
        // the image borrows them instead of copying every payload byte.
        ObjectArtifact artifact = object_write_borrowing(arena, &object, object_format_for_target(invocation.target));
        result->object_write_statistics = artifact.statistics;
        if (artifact.error != OBJECT_ERROR_NONE)
        {
            result->error = COMPILER_DRIVER_ERROR_OBJECT;
            result->object_error = artifact.error;
            result->diagnostic = artifact.error == OBJECT_ERROR_UNSUPPORTED_ALIGNMENT ? S8("COFF section alignment exceeds the 8192-byte format limit")
                                 : artifact.error == OBJECT_ERROR_CAPACITY
                                     ? string_format(arena, S8("native {S8} object exceeds the object writer's limits (section count, string-table offsets or size)"),
                                                     object_format_name(artifact.format))
                                     : string_format(arena, S8("native object serialization failed with error {u32}"), (u32)artifact.error);
            return;
        }
        WORK_LEDGER_RECORD(OUTPUT_OBJECT_BYTES, artifact.bytes.length);
        String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_object_path(arena, invocation.input_paths[0]);
        u32 slice_count = 0;
        ByteSlice* slices = object_artifact_slices(arena, artifact, &slice_count);
        if (compiler_driver_publish_slices(arena, output, slices, slice_count, result) && metrics)
        {
            metrics->object_file_bytes = artifact.bytes.length;
        }
        return;
    }
    ObjectFile link_inputs[3] = {object};
    u32 link_input_count = 1;
    if (compiler_driver_windows_runtime_object_target(invocation.target))
    {
        link_inputs[link_input_count++] = link_windows_runtime_object(arena, invocation.target);
        ObjectFile runtime = link_windows_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, link_inputs, link_input_count))
        {
            link_inputs[link_input_count++] = runtime;
        }
    }
    if (compiler_driver_elf_runtime_object_target(invocation.target))
    {
        ObjectFile runtime = link_elf_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, link_inputs, link_input_count))
        {
            link_inputs[link_input_count++] = runtime;
        }
    }
    LinkObjectResult linked = link_objects(arena, link_inputs, link_input_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                               .alias_single_input_sections = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_LINK;
        result->native_link.error = linked.error;
        result->native_link.symbol = linked.symbol;
        result->diagnostic = linked.symbol.length ? string_format(arena, S8("C object linking failed with {S8} on symbol '{S8}'"), link_error_name(linked.error), linked.symbol)
                                                 : string_format(arena, S8("C object linking failed with {S8}"), link_error_name(linked.error));
        return;
    }
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_executable_path(invocation.target);
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_target_dynamic_libraries(arena, invocation, 0, &linked.object);
    if (dynamic_libraries.missing_request.length)
    {
        result->error = COMPILER_DRIVER_ERROR_LINK;
        result->diagnostic = dynamic_libraries.unsupported_script_path.length
                                 ? string_format(arena, S8("unsupported GNU linker script {S8} requested by -l{S8}"),
                                                 dynamic_libraries.unsupported_script_path, dynamic_libraries.missing_request)
                                 : string_format(arena, S8("cannot find -l{S8}"), dynamic_libraries.missing_request);
        compiler_driver_dynamic_libraries_release(&dynamic_libraries);
        return;
    }
    if (dynamic_libraries.missing_runtime_symbol.length)
    {
        result->error = COMPILER_DRIVER_ERROR_LINK;
        result->native_link.error = LINK_ERROR_UNRESOLVED_SYMBOL;
        result->native_link.symbol = dynamic_libraries.missing_runtime_symbol;
        result->diagnostic = string_format(arena, S8("target library {S8} does not provide callable compiler helper: {S8}"),
                                          dynamic_libraries.runtime_failure_library, dynamic_libraries.missing_runtime_symbol);
        compiler_driver_dynamic_libraries_release(&dynamic_libraries);
        return;
    }
    result->native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = output,
                                                    .entry_symbol = invocation.entry_symbol.length ? invocation.entry_symbol
                                                                                                   : compiler_driver_default_entry_symbol(invocation.target),
                                                    .sysroot = invocation.sysroot,
                                                    .library_paths = invocation.library_paths,
                                                    .framework_paths = invocation.framework_paths,
                                                    .frameworks = invocation.frameworks,
                                                    .linker_arguments = invocation.linker_arguments,
                                                    .library_path_count = invocation.library_path_count,
                                                    .framework_path_count = invocation.framework_path_count,
                                                    .framework_count = invocation.framework_count,
                                                    .linker_argument_count = invocation.linker_argument_count,
                                                    .dynamic_libraries = dynamic_libraries.pointer,
                                                    .dynamic_library_count = dynamic_libraries.count,
                                                    .runtime_exported_symbols = dynamic_libraries.runtime.exported_symbols,
                                                    .runtime_data_symbols = dynamic_libraries.runtime.exported_data_symbols,
                                                    .runtime_versioned_symbols = dynamic_libraries.runtime.versioned_symbols,
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_data_symbol_count = dynamic_libraries.runtime.exported_data_symbol_count,
                                                    .runtime_versioned_symbol_count = dynamic_libraries.runtime.versioned_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                    .image_kind = (u8)invocation.image_kind,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    WORK_LEDGER_RECORD(OUTPUT_LINK_IMAGE_BYTES, result->native_link.executable.length);
    if (result->native_link.error != LINK_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_LINK;
        result->diagnostic = compiler_driver_native_link_diagnostic(arena, invocation, result->native_link);
    }
}

// Preserve each assembled section's own name and its code/data or DWARF
// identity; the object writer derives allocation flags from that identity.
BUSTER_GLOBAL_LOCAL ObjectSectionKind compiler_driver_assembly_section_kind(AssemblyUnitSectionKind kind)
{
    ObjectSectionKind object_kind;
    switch (kind)
    {
    case ASSEMBLY_UNIT_SECTION_TEXT: object_kind = OBJECT_SECTION_TEXT; break;
    case ASSEMBLY_UNIT_SECTION_READ_ONLY_DATA: object_kind = OBJECT_SECTION_READ_ONLY_DATA; break;
    case ASSEMBLY_UNIT_SECTION_DATA: object_kind = OBJECT_SECTION_DATA; break;
    case ASSEMBLY_UNIT_SECTION_INIT_ARRAY: object_kind = OBJECT_SECTION_INIT_ARRAY; break;
    case ASSEMBLY_UNIT_SECTION_FINI_ARRAY: object_kind = OBJECT_SECTION_FINI_ARRAY; break;
    case ASSEMBLY_UNIT_SECTION_THREAD_LOCAL_DATA: object_kind = OBJECT_SECTION_THREAD_LOCAL_DATA; break;
    case ASSEMBLY_UNIT_SECTION_THREAD_LOCAL_ZERO: object_kind = OBJECT_SECTION_THREAD_LOCAL_ZERO; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_INFO: object_kind = OBJECT_SECTION_DEBUG_INFO; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_ABBREV: object_kind = OBJECT_SECTION_DEBUG_ABBREV; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_LINE: object_kind = OBJECT_SECTION_DEBUG_LINE; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_STR: object_kind = OBJECT_SECTION_DEBUG_STR; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_LOC: object_kind = OBJECT_SECTION_DEBUG_LOC; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_RANGES: object_kind = OBJECT_SECTION_DEBUG_RANGES; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_ADDR: object_kind = OBJECT_SECTION_DEBUG_ADDR; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_STR_OFFSETS: object_kind = OBJECT_SECTION_DEBUG_STR_OFFSETS; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_LINE_STR: object_kind = OBJECT_SECTION_DEBUG_LINE_STR; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_RNGLISTS: object_kind = OBJECT_SECTION_DEBUG_RNGLISTS; break;
    case ASSEMBLY_UNIT_SECTION_DEBUG_LOCLISTS: object_kind = OBJECT_SECTION_DEBUG_LOCLISTS; break;
    case ASSEMBLY_UNIT_SECTION_ZERO:
    case ASSEMBLY_UNIT_SECTION_KIND_COUNT:
    default: object_kind = OBJECT_SECTION_ZERO; break;
    }
    return object_kind;
}

// The assembler reports a relocation in its own vocabulary, which is wider
// than the object model's. A family the object cannot express is refused
// rather than written without its relocation.
BUSTER_GLOBAL_LOCAL bool compiler_driver_assembly_relocation_kind(AssemblyRelocationKind kind, ObjectRelocationKind* object_kind)
{
    bool valid = true;
    switch (kind)
    {
    case ASSEMBLY_RELOCATION_X86_PC32: *object_kind = OBJECT_RELOCATION_X86_64_PC32; break;
    case ASSEMBLY_RELOCATION_X86_PC64: *object_kind = OBJECT_RELOCATION_X86_64_PC64; break;
    case ASSEMBLY_RELOCATION_AARCH64_PREL32: *object_kind = OBJECT_RELOCATION_AARCH64_PREL32; break;
    case ASSEMBLY_RELOCATION_AARCH64_PREL64: *object_kind = OBJECT_RELOCATION_AARCH64_PREL64; break;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE32: *object_kind = OBJECT_RELOCATION_ABSOLUTE32; break;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE64: *object_kind = OBJECT_RELOCATION_ABSOLUTE64; break;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED: *object_kind = OBJECT_RELOCATION_X86_64_ABSOLUTE32S; break;
    case ASSEMBLY_RELOCATION_AARCH64_BRANCH26: *object_kind = OBJECT_RELOCATION_AARCH64_JUMP26; break;
    case ASSEMBLY_RELOCATION_AARCH64_CALL26: *object_kind = OBJECT_RELOCATION_AARCH64_CALL26; break;
    case ASSEMBLY_RELOCATION_AARCH64_ADR_PREL_LO21: *object_kind = OBJECT_RELOCATION_AARCH64_ELF_ADR_PREL_LO21; break;
    default: valid = false; break;
    }
    return valid;
}

// One assembly input, from source text to the same three outputs a C input
// reaches. The assembler is the whole front end here: there is no
// preprocessor, no IR, and no code generation between the file and the object.
// The assemble-and-emit half shared by a `.s` input and a preprocessed `.S`
// one: the caller owns the source text's lifetime, and the encoder keeps
// nothing that points into it.
BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_assembly_source(Arena* arena, CompilerDriverInvocation invocation, String8 source,
                                                                                  String8 path, bool suppress_object_write, CompilerDriverDiagnosticCollector* diagnostics,
                                                                                  CPreprocessResult* preprocess, String8 split_source, CompilerDriverUnitMetrics* metrics)
{
    CompilerDriverResult result = {0};
    if (invocation.emit_llvm_bitcode || compiler_driver_target_is_wasm(invocation.target) || invocation.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("assembly input has no LLVM bitcode, WebAssembly, or eBPF emission");
        return result;
    }
    AssemblyUnitResult unit = assembly_unit_encode(arena, source,
                                                   (AssemblyEncodeOptions){
                                                       .target = invocation.target,
                                                       .syntax = invocation.target.cpu_arch == CPU_ARCH_X86_64 ? invocation.assembly_syntax
                                                                                                              : ASSEMBLY_SYNTAX_DEFAULT,
                                                   });
    if (unit.diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        for (u32 index = 0; index < unit.diagnostic_count; index += 1)
        {
            CompilerDiagnostic diagnostic = compiler_driver_assembly_diagnostic(source, path, unit.diagnostics[index]);
            if (preprocess)
            {
                diagnostic = compiler_driver_preprocessed_assembly_diagnostic(arena, preprocess, split_source, path, diagnostic);
            }
            compiler_driver_collect_diagnostic(diagnostics, diagnostic);
            if (!index) result.diagnostic = compiler_diagnostic_render(arena, diagnostic);
        }
        return result;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
    {
        return result;
    }
    ObjectFile object = {
        .target = invocation.target,
        .sections = arena_allocate(arena, ObjectSection, unit.section_count ? unit.section_count : 1),
        .symbols = arena_allocate(arena, ObjectSymbol, unit.symbol_count ? unit.symbol_count : 1),
        .relocations = arena_allocate(arena, ObjectRelocation, unit.relocation_count ? unit.relocation_count : 1),
        .section_count = unit.section_count,
        .symbol_count = unit.symbol_count,
        .requires_executable_stack = unit.requires_executable_stack,
        .executable_stack_source = unit.requires_executable_stack ? path : (String8){0},
    };
    for (u32 index = 0; index < unit.section_count; index += 1)
    {
        AssemblyUnitSection section = unit.sections[index];
        object.sections[index] = (ObjectSection){
            .name = section.name,
            .data = section.data,
            .virtual_size = section.zero_size,
            .kind = compiler_driver_assembly_section_kind(section.kind),
            .alignment = section.alignment,
        };
    }
    for (u32 index = 0; index < unit.symbol_count; index += 1)
    {
        AssemblyUnitSymbol symbol = unit.symbols[index];
        object.symbols[index] = (ObjectSymbol){
            .name = symbol.name,
            .value = symbol.value,
            .size = symbol.size,
            .section = symbol.defined ? symbol.section : OBJECT_SECTION_UNDEFINED,
            .kind = symbol.function ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
            .global = symbol.global,
            .weak = symbol.weak,
            .hidden = symbol.hidden,
            .untyped = symbol.untyped,
        };
    }
    for (u32 index = 0; index < unit.relocation_count; index += 1)
    {
        AssemblyUnitRelocation relocation = unit.relocations[index];
        ObjectRelocationKind kind = OBJECT_RELOCATION_X86_64_PC32;
        if (!compiler_driver_assembly_relocation_kind(relocation.kind, &kind))
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic = string_format(arena, S8("{S8}: relocation family {u32} has no object representation"), path, (u32)relocation.kind);
            return result;
        }
        if (relocation.plt)
        {
            kind = OBJECT_RELOCATION_X86_64_PLT32;
        }
        object.relocations[object.relocation_count++] = (ObjectRelocation){
            // The assembler's addend already carries the distance from the
            // relocated field to the end of its instruction, which is what an
            // ELF PC-relative addend is; nothing is added or removed here.
            .addend = relocation.addend,
            .offset = relocation.offset,
            .section = relocation.section,
            .symbol = relocation.symbol,
            .kind = kind,
        };
    }
    result.object = object;
    result.has_object = true;
    if (metrics)
    {
        metrics->function_symbol_limit = object.symbol_count;
    }
    compiler_driver_emit_object_output(arena, invocation, object, suppress_object_write, &result, metrics);
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_assembly_single(Arena* arena, CompilerDriverInvocation invocation,
                                                                                  bool suppress_object_write, CompilerDriverDiagnosticCollector* diagnostics,
                                                                                  CompilerDriverUnitMetrics* metrics)
{
    CompilerDriverResult result = {0};
    String8 path = invocation.input_paths[0];
    FileMapRead source_file = file_map_read(arena, path, (FileReadOptions){0});
    if (!source_file.bytes.pointer)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not read {S8}"), path);
        file_map_unmap(source_file);
        return result;
    }
    String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        // An assembly unit is already what the preprocessor would have
        // produced, so -E hands the text back unchanged.
        compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_EMIT);
        result.output = string_duplicate_arena(arena, source, false);
        if (invocation.output_path.length)
        {
            compiler_driver_publish(arena, invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output), &result);
        }
        file_map_unmap(source_file);
        return result;
    }
    result = compiler_driver_execute_assembly_source(arena, invocation, source, path, suppress_object_write, diagnostics, 0, (String8){0}, metrics);
    file_map_unmap(source_file);
    return result;
}

// GNU's `.S` runs the C preprocessor over the assembly text before it is
// assembled.  The -E printer reproduces line structure and adjacency from
// the source map, so `%rax`, `1f` and `.globl` survive the round trip, and
// the preprocess itself runs with assembly_comment_lines: a `#` line whose
// word is no directive is GNU-as commentary, not an error.  CPython's
// Python/asm_trampoline.S is the load-bearing case -- one trampoline body
// selected by #ifdef per architecture.
BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_preprocessed_assembly_single(Arena* arena, CompilerDriverInvocation invocation,
                                                                                               bool suppress_object_write, CompilerDriverDiagnosticCollector* diagnostics,
                                                                                               CompilerDriverUnitMetrics* metrics)
{
    CompilerDriverResult result = {0};
    String8 path = invocation.input_paths[0];
    FileMapRead source_file = file_map_read(arena, path, (FileReadOptions){0});
    if (!source_file.bytes.pointer)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not read {S8}"), path);
        file_map_unmap(source_file);
        return result;
    }
    CPreprocessorDefinition* definitions = arena_allocate(arena, CPreprocessorDefinition, invocation.definition_count);
    for (u32 index = 0; index < invocation.definition_count; index += 1)
    {
        definitions[index] = compiler_driver_c_definition(invocation.definitions[index]);
    }
    // GNU-as spells an immediate `$NAME`, and the C lexer reads `$` as an
    // identifier character, which would glue the prefix onto a macro name
    // and keep it from expanding.  A space after every `$` splits the two
    // the way GNU cpp's assembler mode tokenizes them, and the assembler
    // reads `$ 0` and `$0` alike; quoted regions keep their bytes.
    String8 raw = BYTE_SLICE_TO_STRING(8, source_file.bytes);
    char8* split = arena_allocate(arena, char8, raw.length * 2 + 1);
    u64 split_length = 0;
    bool in_string = false;
    char8 quote = 0;
    for (u64 byte_index = 0; byte_index < raw.length; byte_index += 1)
    {
        char8 byte = raw.pointer[byte_index];
        split[split_length++] = byte;
        if (in_string)
        {
            if (byte == '\\' && byte_index + 1 < raw.length)
            {
                split[split_length++] = raw.pointer[++byte_index];
            }
            else if (byte == quote)
            {
                in_string = false;
            }
        }
        else if (byte == '"' || byte == '\'')
        {
            in_string = true;
            quote = byte;
        }
        else if (byte == '$')
        {
            split[split_length++] = ' ';
        }
    }
    CPreprocessResult preprocess = c_preprocess(arena, (String8){.pointer = split, .length = split_length},
                                                (CPreprocessOptions){
                                                    .macro_operations = invocation.macro_operations,
                                                    .definitions = definitions,
                                                    .undefinitions = invocation.undefinitions,
                                                    .include_paths = invocation.include_paths,
                                                    .system_include_paths = invocation.system_include_paths,
                                                    .source_path = path,
                                                    .source_identity = source_file.identity,
                                                    .target = invocation.target,
                                                    .data_layout = target_data_layout(invocation.target),
                                                    .dialect = compiler_driver_preprocess_dialect(invocation.c_dialect),
                                                    .position_independent_level = invocation.position_independent_level,
                                                    .position_independent_executable = invocation.position_independent_executable,
                                                    .macro_operation_count = invocation.macro_operation_count,
                                                    .definition_count = invocation.definition_count,
                                                    .undefinition_count = invocation.undefinition_count,
                                                    .include_path_count = invocation.include_path_count,
                                                    .system_include_path_count = invocation.system_include_path_count,
                                                    .assembly_comment_lines = true,
                                                    .retain_output_spacing = true,
                                                    .dump_macros = invocation.dump_macros && invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS,
                                                });
    file_map_unmap(source_file);
    String8 preprocessing_error = compiler_driver_publish_c_diagnostics(arena, diagnostics, &preprocess, preprocess.diagnostics,
                                                                          preprocess.diagnostic_count, path, (String8){.pointer = split, .length = split_length});
    if (preprocess.error_count)
    {
        result.error = COMPILER_DRIVER_ERROR_TOKENIZE;
        result.tokenizer_error_count = (u32)preprocess.error_count;
        result.diagnostic = preprocessing_error;
        c_preprocess_release(&preprocess);
        return result;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_EMIT);
    }
    String8 source = invocation.dump_macros && invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS ? c_preprocess_detail(preprocess)->macro_dump
                                                                                                      : compiler_driver_preprocess_text(arena, preprocess, UINT64_MAX, 0, false);
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.output = source;
        if (invocation.output_path.length)
        {
            compiler_driver_publish(arena, invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output), &result);
        }
        c_preprocess_release(&preprocess);
        return result;
    }
    result = compiler_driver_execute_assembly_source(arena, invocation, source, path, suppress_object_write, diagnostics, &preprocess, (String8){.pointer = split, .length = split_length}, metrics);
    c_preprocess_release(&preprocess);
    return result;
}

// The spelling arena holds the text every name in the frontend's IR points
// into, and the driver gives it back as soon as the unit is compiled. An
// object the result carries must therefore not name anything inside it: copy
// each string (and any section payload) that does into the result arena.
BUSTER_GLOBAL_LOCAL bool compiler_driver_in_released_arena(Arena const* released, void const* pointer, u64 length)
{
    u8 const* begin = (u8 const*)released;
    u8 const* address = (u8 const*)pointer;
    return length != 0 && address >= begin && address < begin + released->reserved_size;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_detach_string(Arena* arena, Arena const* released, String8 value)
{
    String8 result = value;
    if (compiler_driver_in_released_arena(released, value.pointer, value.length))
    {
        result = string_duplicate_arena(arena, value, false);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_detach_object(Arena* arena, CPreprocessResult const* preprocess, ObjectFile* object)
{
    Arena const* released = preprocess->recovery ? preprocess->recovery->spelling_arena : 0;
    if (released)
    {
        for (u32 index = 0; index < object->section_count; index += 1)
        {
            ObjectSection* section = &object->sections[index];
            section->name = compiler_driver_detach_string(arena, released, section->name);
            if (compiler_driver_in_released_arena(released, section->data.pointer, section->data.length))
            {
                u8* copy = arena_allocate(arena, u8, section->data.length);
                memcpy(copy, section->data.pointer, section->data.length);
                section->data.pointer = copy;
            }
        }
        for (u32 index = 0; index < object->symbol_count; index += 1)
        {
            object->symbols[index].name = compiler_driver_detach_string(arena, released, object->symbols[index].name);
        }
        for (u32 index = 0; index < object->comdat_count; index += 1)
        {
            object->comdats[index].key = compiler_driver_detach_string(arena, released, object->comdats[index].key);
        }
        for (u32 index = 0; index < object->debug_module_count; index += 1)
        {
            object->debug_modules[index].name = compiler_driver_detach_string(arena, released, object->debug_modules[index].name);
        }
        object->diagnostic = compiler_driver_detach_string(arena, released, object->diagnostic);
    }
}

BUSTER_GLOBAL_LOCAL void compiler_driver_investigation_tokens(InvestigationCapture* capture, CPreprocessResult preprocess)
{
    Sha256 hash;
    sha256_init(&hash);
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        CToken token = preprocess.tokens[index];
        String8 spelling = token.kind == C_TOKEN_END_OF_FILE ? (String8){0} : c_token_spelling(preprocess.spelling_base, token);
        u8 frame[12];
        ByteWriter writer = byte_writer_make(frame, sizeof(frame));
        byte_writer_emit_u32_le(&writer, token.kind);
        byte_writer_emit_u32_le(&writer, (u32)spelling.length);
        byte_writer_emit_u32_le(&writer, (u32)(spelling.length >> 32));
        sha256_add(&hash, frame, sizeof(frame));
        if (spelling.length)
        {
            sha256_add(&hash, spelling.pointer, spelling.length);
        }
    }
    sha256_finish_hex(&hash, capture->translation_sha256);
}

BUSTER_GLOBAL_LOCAL void compiler_driver_finish_investigation(Arena* arena, CompilerDriverInvocation invocation,
                                                             InvestigationCapture* capture, ObjectFile const* object, CompilerDriverResult* result)
{
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_object_path(arena, invocation.input_paths[0]);
    String8 capture_absolute = os_path_absolute(arena, invocation.investigation_path, false);
    String8 output_absolute = os_path_absolute(arena, output, false);
    String8 input_absolute = os_path_absolute(arena, invocation.input_paths[0], false);
    bool aliases = capture_absolute.length && (string_equal(capture_absolute, output_absolute) || string_equal(capture_absolute, input_absolute));
    FileMapRead artifact = {0};
    ByteSlice sidecar = {0};
    if (aliases)
    {
        capture->diagnostic = S8("investigation capture path resolves to its input or object output");
    }
    else if (!capture->found && !capture->diagnostic.length)
    {
        capture->diagnostic = S8("investigation function has no retained machine encoding (missing function or canonical fallback)");
    }
    if (!capture->diagnostic.length)
    {
        TimeDataType start = timestamp_take();
        capture->artifact_path = output;
        artifact = file_map_read(arena, output, (FileReadOptions){0});
        if (!artifact.bytes.pointer || !investigation_bind_object(arena, capture, object, artifact.bytes))
        {
            if (!capture->diagnostic.length) capture->diagnostic = S8("could not read completed investigation object");
        }
        capture->capture_ns += timestamp_ns_between(start, timestamp_take());
        if (!capture->diagnostic.length)
        {
            sidecar = investigation_serialize(arena, capture);
            if (!sidecar.pointer) capture->diagnostic = S8("investigation capture exceeds its format bounds");
        }
    }
    if (!capture->diagnostic.length && !file_publish(invocation.investigation_path, sidecar))
    {
        capture->diagnostic = S8("could not publish investigation capture");
    }
    file_map_unmap(artifact);
    if (capture->diagnostic.length)
    {
        result->error = COMPILER_DRIVER_ERROR_FILE_WRITE;
        result->diagnostic = capture->diagnostic;
    }
}

static CompilerDriverResult compiler_driver_execute_c_single(Arena* arena, CompilerDriverInvocation invocation, bool suppress_object_write,
                                                             CompilerDriverDiagnosticCollector* warnings, CompilerDriverUnitMetrics* metrics)
{
    CompilerDriverResult result = {
        .error = invocation.error,
        .diagnostic = invocation.diagnostic,
    };
    FileMapRead source_file = {0};
    // Filled once the source is preprocessed; the end label releases its
    // private arenas, which nothing the result holds points into.
    CPreprocessResult preprocess = {0};
    if (!arena || invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        return result;
    }
    if (invocation.input_count == 1 && compiler_driver_preprocessed_assembly_input(invocation.input_paths[0]))
    {
        return compiler_driver_execute_preprocessed_assembly_single(arena, invocation, suppress_object_write, warnings, metrics);
    }
    if (invocation.input_count == 1 &&
        compiler_driver_assembly_input(compiler_driver_input_language(invocation, 0), invocation.input_paths[0]))
    {
        return compiler_driver_execute_assembly_single(arena, invocation, suppress_object_write, warnings, metrics);
    }
    if (invocation.input_count != 1 ||
        !compiler_driver_c_input(compiler_driver_input_language(invocation, 0), invocation.input_paths[0]))
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("the C frontend currently requires exactly one C input");
        goto end;
    }
    // The `-` input arrives already read; it has no file identity.
    if (string_equal(invocation.input_paths[0], S8("-")))
    {
        source_file.bytes = (ByteSlice){.pointer = (u8*)invocation.standard_input.pointer, .length = invocation.standard_input.length};
    }
    else
    {
        source_file = file_map_read(arena, invocation.input_paths[0], (FileReadOptions){0});
    }
    ByteSlice bytes = source_file.bytes;
    if (!bytes.pointer)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not read {S8}"), invocation.input_paths[0]);
        goto end;
    }
    compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_PREPROCESS);
    CPreprocessorDefinition* definitions = arena_allocate(arena, CPreprocessorDefinition, invocation.definition_count);
    for (u32 index = 0; index < invocation.definition_count; index += 1)
    {
        definitions[index] = compiler_driver_c_definition(invocation.definitions[index]);
    }
    WORK_LEDGER_PHASE(PREPROCESS);
    preprocess = c_preprocess(arena, BYTE_SLICE_TO_STRING(8, bytes),
                                                (CPreprocessOptions){
                                                    .macro_operations = invocation.macro_operations,
                                                    .definitions = definitions,
                                                    .undefinitions = invocation.undefinitions,
                                                    .include_paths = invocation.include_paths,
                                                    .system_include_paths = invocation.system_include_paths,
                                                    .source_path = invocation.input_paths[0],
                                                    .source_identity = source_file.identity,
                                                    .target = invocation.target,
                                                    .data_layout = target_data_layout(invocation.target),
                                                    .dialect = compiler_driver_preprocess_dialect(invocation.c_dialect),
                                                    .position_independent_level = invocation.position_independent_level,
                                                    .position_independent_executable = invocation.position_independent_executable,
                                                    .macro_operation_count = invocation.macro_operation_count,
                                                    .definition_count = invocation.definition_count,
                                                    .undefinition_count = invocation.undefinition_count,
                                                    .include_path_count = invocation.include_path_count,
                                                    .system_include_path_count = invocation.system_include_path_count,
                                                    .already_preprocessed = compiler_driver_c_input_phase(compiler_driver_input_language(invocation, 0), invocation.input_paths[0]) == COMPILER_DRIVER_C_INPUT_PREPROCESSED,
                                                    .omit_spelled_bytes = invocation.omit_spelled_bytes,
                                                    .retain_output_spacing = invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS,
                                                    .dump_macros = invocation.dump_macros && invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS,
                                                    .source_cache = invocation.source_cache,
                                                    .preserve_spellings = invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS,
                                                });
    // Reported even when a later stage fails: the units the frontend read are
    // measured by then, and a failing compile is exactly when the size of
    // what it read is worth knowing.
    CPreprocessDetail const* preprocess_detail = c_preprocess_detail(preprocess);
    result.source_lexed = preprocess_detail->source_lexed;
    result.source_unique = preprocess_detail->source_unique;
    result.lexed_files = preprocess_detail->lexed_files;
    result.lexed_file_count = preprocess_detail->lexed_file_count;
    result.preprocessed = preprocess_detail->preprocessed;
    String8 preprocessing_error = compiler_driver_publish_c_diagnostics(arena, warnings, &preprocess, preprocess.diagnostics,
                                                                          preprocess.diagnostic_count, invocation.input_paths[0], (String8){0});
    result.tokenizer_warning_count = (u32)preprocess.warning_count;
    if (preprocess.error_count)
    {
        result.error = COMPILER_DRIVER_ERROR_TOKENIZE;
        result.tokenizer_error_count = (u32)preprocess.error_count;
        result.diagnostic = preprocessing_error;
        goto end;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_EMIT);
        result.output = invocation.dump_macros ? c_preprocess_detail(preprocess)->macro_dump : compiler_driver_preprocess_text(arena, preprocess, UINT64_MAX, 0, true);
        if (invocation.output_path.length)
        {
            compiler_driver_publish(arena, invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output), &result);
        }
        goto end;
    }
    if (invocation.bootstrap_trace_prefix.length)
    {
        String8 path = string_format_z(arena, S8("{S8}.tokens"), invocation.bootstrap_trace_prefix);
        BootstrapTrace trace = bootstrap_trace_open(arena, path, S8("tokens"));
        bootstrap_trace_u64(&trace, preprocess.token_count);
        for (u64 i = 0; i < preprocess.token_count; i += 1)
        {
            CToken token = preprocess.tokens[i];
            bootstrap_trace_u64(&trace, token.kind);
            bootstrap_trace_string(&trace, token.kind == C_TOKEN_END_OF_FILE ? (String8){0} : c_token_spelling(preprocess.spelling_base, token));
        }
        if (!bootstrap_trace_close(&trace))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_WRITE;
            result.diagnostic = string_format(arena, S8("could not write bootstrap tokens: {S8}"), path);
            goto end;
        }
    }
    WORK_LEDGER_PHASE(PARSE);
    compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_PARSE);
    CParserResult syntax = c_parse_ast(arena, preprocess);
    result.parser_diagnostic_count = syntax.diagnostic_count;
    if (syntax.diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_PARSE;
        result.diagnostic = compiler_driver_publish_c_diagnostics(arena, warnings, &preprocess, syntax.diagnostics,
                                                                  syntax.diagnostic_count, invocation.input_paths[0], (String8){0});
        goto end;
    }
    WORK_LEDGER_PHASE(SEMANTIC);
    compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_ANALYSIS);
    if (invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
    {
        CAnalysisResult semantic = c_analyze_semantics_only(arena, preprocess, syntax);
        result.analysis_diagnostic_count = semantic.diagnostic_count;
        if (semantic.diagnostic_count || !semantic.analysis_complete)
        {
            result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
            if (semantic.diagnostic_count)
            {
                result.diagnostic = compiler_driver_publish_c_diagnostics(arena, warnings, &preprocess, semantic.diagnostics,
                                                                          semantic.diagnostic_count, invocation.input_paths[0], (String8){0});
            }
            else
            {
                result.diagnostic = string_format(arena, S8("{S8}: semantic validation failed"), invocation.input_paths[0]);
            }
        }
        goto end;
    }
    CIRLowerResult lowered = c_analyze_with_options(arena, invocation.input_paths[0], preprocess, syntax, invocation.target,
                                                  (CIRLowerOptions){.disable_direct_ssa = invocation.disable_direct_ssa,
                                                                    .sysv_unnamed_bitfields_integer = invocation.sysv_unnamed_bitfields_integer,
                                                                    .omit_debug_locals = !invocation.debug_info});
    result.analysis_diagnostic_count = lowered.diagnostic_count;
    result.direct_ssa = lowered.direct_ssa;
    result.type_layout = lowered.type_layout;
    if (!lowered.program || lowered.diagnostic_count || !lowered.program->modules || lowered.program->module_count != 1)
    {
        result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
        if (lowered.diagnostic_count)
        {
            result.diagnostic = compiler_driver_publish_c_diagnostics(arena, warnings, &preprocess, lowered.diagnostics,
                                                                      lowered.diagnostic_count, invocation.input_paths[0], (String8){0});
        }
        else
        {
            result.diagnostic = string_format(arena, S8("{S8}: C analysis or lowering did not publish a complete program"), invocation.input_paths[0]);
        }
        goto end;
    }
    compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_IR);
    IrModule* module = &lowered.program->modules[0];
    lowered.program->disable_local_promotion = invocation.disable_local_promotion;
    lowered.program->disable_target_local_promotion = invocation.disable_target_local_promotion;
    lowered.program->fast_passes = invocation.fast_passes;
    lowered.program->measure_fast_passes = invocation.measure_fast_passes;
    WORK_LEDGER_PHASE(PREPARE);
    IrValidationResult validation = ir_prepare_canonical_module(lowered.program, module,
                                                                lowered.canonical_ir_certified && !invocation.bootstrap_trace_prefix.length && !invocation.verify_codegen);
    result.local_promotion = module->local_promotion;
    result.fast = module->fast;
    if (validation.error != IR_VALIDATION_NONE)
    {
        String8 function_name = validation.function.value < module->function_count ? module->functions[validation.function.value].name : S8("<invalid>");
        u32 opcode = IR_OPCODE_COUNT;
        if (validation.function.value < module->function_count)
        {
            IrFunction* failed_function = &module->functions[validation.function.value];
            if (validation.instruction.value < failed_function->instruction_count)
            {
                opcode = (u32)failed_function->instructions[validation.instruction.value].opcode;
            }
        }
        String8 boundary = validation.boundary == IR_VALIDATION_BOUNDARY_CFG_PUBLICATION ? S8("canonical CFG publication") :
                           validation.boundary == IR_VALIDATION_BOUNDARY_LOCAL_PROMOTION_OUTPUT ? S8("local-promotion output") :
                           validation.boundary == IR_VALIDATION_BOUNDARY_FAST_OUTPUT ? S8("FAST output") : S8("canonical input");
        result.error = COMPILER_DRIVER_ERROR_IR;
        result.diagnostic =
            string_format(arena, S8("canonical C IR validation failed: boundary {S8}, error {u32}, function {u32} ('{S8}'), block {u32}, instruction {u32}, opcode {u32}"),
                          boundary, (u32)validation.error, validation.function.value, function_name, validation.block.value, validation.instruction.value, opcode);
        goto end;
    }
    WORK_LEDGER_PHASE(CODEGEN);
    if (invocation.emit_llvm_bitcode || compiler_driver_target_is_wasm(invocation.target) || invocation.target.cpu_arch == CPU_ARCH_BPFEL ||
        invocation.target.cpu_arch == CPU_ARCH_SPIRV_COMPUTE)
    {
        compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_EMIT);
    }
    if (invocation.target.cpu_arch == CPU_ARCH_SPIRV_COMPUTE)
    {
        SpirvArtifact artifact = spirv_emit(arena, lowered.program, module);
        compiler_driver_write_spirv(arena, invocation, lowered.program, module, artifact, &result);
        goto end;
    }
    if (invocation.emit_llvm_bitcode)
    {
        LlvmBitcodeArtifact artifact =
            llvm_bitcode_emit_with_options(arena, lowered.program, module, 1,
                                           compiler_driver_llvm_bitcode_options(invocation.target, invocation.input_paths[0]));
        compiler_driver_write_llvm_bitcode(arena, invocation, lowered.program, module, artifact, &result);
        goto end;
    }
    // The preparation above is the module's validation boundary: it already
    // scanned or certified these exact rows and they have not changed since.
    // Hand that fact to the direct emitters, as native code generation and
    // LLVM bitcode receive it, instead of letting each one re-prepare
    // uncertified and walk the whole module again. (-fverify-codegen, which
    // forces uncertified preparation, is refused for these targets.)
    if (compiler_driver_target_is_wasm(invocation.target))
    {
        WasmOptions options = compiler_driver_wasm_options(invocation.target);
        options.assume_validated = true;
        WasmArtifact artifact = wasm_emit(arena, lowered.program, module, 1, options);
        compiler_driver_write_wasm(arena, invocation, lowered.program, module, artifact, &result);
        goto end;
    }
    if (invocation.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        EbpfOptions options = EBPF_OPTIONS_DEFAULT;
        options.assume_validated = true;
        EbpfArtifact artifact = ebpf_emit_with_options(arena, lowered.program, module, 1, options);
        compiler_driver_write_ebpf(arena, invocation, lowered.program, module, artifact, &result);
        goto end;
    }
    InvestigationCapture* investigation = 0;
    if (invocation.investigation_path.length)
    {
        investigation = arena_allocate(arena, InvestigationCapture, 1);
        *investigation = (InvestigationCapture){
            .function_name = invocation.investigation_function,
            .revision = investigation_compiler_revision(),
            .configuration = invocation.investigation_configuration,
            .input_path = invocation.input_paths[0],
            .allocator = invocation.register_allocator,
            .cpu = (u32)invocation.target.cpu_arch,
            .os = (u32)invocation.target.os,
        };
        investigation_digest(bytes, investigation->input_sha256);
        compiler_driver_investigation_tokens(investigation, preprocess);
        TargetCpuFeatures features = target_cpu_features_effective(invocation.target);
        investigation->target = string_format(arena, S8("arch={u32} model={u32} os={u32} version={u16}.{u8}.{u8} char={u32}"),
                                              (u32)invocation.target.cpu_arch, (u32)invocation.target.cpu_model, (u32)invocation.target.os,
                                              invocation.target.os_version_major, invocation.target.os_version_minor, invocation.target.os_version_patch,
                                              (u32)invocation.target.plain_char_policy);
        for (u32 word = 0; word < TARGET_CPU_FEATURE_WORD_COUNT; word += 1)
        {
            investigation->target = string_format(arena, S8("{S8} features{u32}={u64:x}"), investigation->target, word, features.words[word]);
        }
    }
    compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_CODEGEN);
    BootstrapTrace mir_trace = {0};
    if (invocation.bootstrap_trace_prefix.length)
    {
        String8 ir_path = string_format_z(arena, S8("{S8}.ir"), invocation.bootstrap_trace_prefix);
        BootstrapTrace trace = bootstrap_trace_open(arena, ir_path, S8("canonical IR"));
        bootstrap_trace_ir(&trace, lowered.program, module);
        if (!bootstrap_trace_close(&trace))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_WRITE;
            result.diagnostic = string_format(arena, S8("could not write bootstrap IR: {S8}"), ir_path);
            goto end;
        }
        String8 mir_path = string_format_z(arena, S8("{S8}.mir"), invocation.bootstrap_trace_prefix);
        mir_trace = bootstrap_trace_open(arena, mir_path, S8("selected MIR"));
        bootstrap_trace_u64(&mir_trace, (u64)invocation.target.cpu_arch);
        bootstrap_trace_u64(&mir_trace, invocation.register_allocator);
        bootstrap_trace_u64(&mir_trace, invocation.position_independent);
        if (mir_trace.failed)
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_WRITE;
            result.diagnostic = string_format(arena, S8("could not open bootstrap MIR: {S8}"), mir_path);
            goto end;
        }
    }
    CodegenModule code = codegen_generate_canonical_module_with_trace(arena, lowered.program, module, invocation.target,
                                                           (CodegenModuleOptions){
                                                               .investigation = investigation,
                                                               .debug_info = invocation.debug_info,
                                                               .assume_validated = true,
                                                               .verify_invariants = invocation.verify_codegen,
                                                               .record_fallbacks = invocation.record_codegen_fallbacks,
                                                               .position_independent = invocation.position_independent,
                                                               .register_allocator = invocation.register_allocator,
                                                               .assembly_syntax = (u8)invocation.assembly_syntax,
                                                           }, invocation.bootstrap_trace_prefix.length ? &mir_trace : 0);
    if (invocation.bootstrap_trace_prefix.length)
    {
        if (!bootstrap_trace_close(&mir_trace))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_WRITE;
            result.diagnostic = S8("could not complete bootstrap MIR trace");
            goto end;
        }
        if (mir_trace.invalid_mir)
        {
            result.error = COMPILER_DRIVER_ERROR_IR;
            result.diagnostic = string_format(arena, S8("bootstrap MIR validation failed in '{S8}': error {u32} ({S8}), block {u32}, instruction {u32}, operand {u32}"),
                                              mir_trace.invalid_function, (u32)mir_trace.invalid_validation.error,
                                              machine_verify_error_name(mir_trace.invalid_validation.error), mir_trace.invalid_validation.block,
                                              mir_trace.invalid_validation.instruction, mir_trace.invalid_validation.operand);
            goto end;
        }
    }
    result.codegen_statistics = code.statistics;
    if (code.fallback_record_count)
    {
        result.fallback_records = arena_allocate(arena, CompilerDriverFallbackRecord, code.fallback_record_count);
        result.fallback_record_count = code.fallback_record_count;
        for (u32 index = 0; index < code.fallback_record_count; index += 1)
        {
            CodegenFallbackRecord record = code.fallback_records[index];
            CompilerDiagnosticLocation location = compiler_driver_backend_location(lowered.program, module, record.function, IR_INSTRUCTION_ID_INVALID);
            result.fallback_records[index] = (CompilerDriverFallbackRecord){
                .source = string_duplicate_arena(arena, location.path.length ? location.path : invocation.input_paths[0], false),
                .function = string_duplicate_arena(arena, module->functions[record.function.value].name, false), .codegen = record,
                .line = location.position.line, .column = location.position.column,
            };
        }
    }
    result.codegen_error = code.error;
    if (code.error != CODEGEN_ERROR_NONE && code.failed_in_assembly)
    {
        // A module-level assembly block has no function and no IR
        // instruction, so the function-shaped report above it would name
        // whichever C function happens to come next in the file. Name the
        // block instead: where the `__asm__` was written, which line of it
        // stopped, and what that line says.
        IrModuleAssembly assembly = code.failed_assembly < module->assembly_count ? module->assemblies[code.failed_assembly] : (IrModuleAssembly){0};
        IrSourcePosition position = ir_source_position(lowered.program, assembly.source_range);
        String8 line = {0};
        u32 line_number = 0;
        u64 line_start = 0;
        while (line_start < assembly.source.length && line_number < code.failed_assembly_line)
        {
            u64 line_end = line_start;
            while (line_end < assembly.source.length && assembly.source.pointer[line_end] != '\n')
            {
                line_end += 1;
            }
            line_number += 1;
            line = (String8){
                .pointer = assembly.source.pointer + line_start,
                .length = line_end - line_start,
            };
            line_start = line_end < assembly.source.length ? line_end + 1 : assembly.source.length;
        }
        CodegenModule context = code;
        context.failed_function = IR_FUNCTION_ID_INVALID;
        context.failed_instruction = IR_INSTRUCTION_ID_INVALID;
        context.failed_opcode = IR_OPCODE_COUNT;
        CompilerDiagnosticBackend backend = compiler_driver_backend_context(arena, invocation, lowered.program, module, context);
        backend.opcode = S8("module-assembly");
        IrSource* source = ir_source_from_id(&lowered.program->sources, (IrSourceId){.value = position.source});
        CompilerDiagnostic diagnostic = {
            .code = compiler_driver_codegen_error_name(code.error), .backend = &backend,
            .primary = {.path = source ? source->path : (String8){0}, .range = assembly.source_range,
                        .position = position, .has_range = position.line != 0},
            .message = string_format(arena, S8("C module assembly refused: kind={S8}, block {u32}, block line {u32} ('{S8}')"),
                                     compiler_driver_codegen_error_name(code.error), code.failed_assembly, code.failed_assembly_line, line),
        };
        diagnostic.primary.original_position = ir_source_map_original_position(&lowered.program->source_map, assembly.source_range.offset);
        IrSource* original = ir_source_from_id(&lowered.program->sources, (IrSourceId){.value = diagnostic.primary.original_position.source});
        if (diagnostic.primary.original_position.line && original) diagnostic.primary.original_path = original->path;
        compiler_driver_collect_diagnostic(warnings, diagnostic);
        result.error = COMPILER_DRIVER_ERROR_CODEGEN;
        result.diagnostic = compiler_diagnostic_render(arena, diagnostic);
        goto end;
    }
    if (code.error != CODEGEN_ERROR_NONE)
    {
        CompilerDiagnosticBackend backend = compiler_driver_backend_context(arena, invocation, lowered.program, module, code);
        CompilerDiagnostic diagnostic = {
            .code = compiler_driver_codegen_error_name(code.error), .backend = &backend,
            .primary = compiler_driver_backend_location(lowered.program, module, code.failed_function, code.failed_instruction),
        };
        diagnostic.message = code.failed_opcode == IR_OPCODE_INLINE_ASSEMBLY &&
                                     code.error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION &&
                                     !string_equal(code.failure_reason, codegen_fallback_reason_string(CODEGEN_FALLBACK_OPCODE))
            ? string_format(arena, S8("C code generation refused in function '{S8}': {S8}"), backend.function, code.failure_reason)
            : code.failed_machine_verification.error != MACHINE_VERIFY_NONE
            ? string_format(arena,
                S8("C code generation refused: kind={S8} target={S8} allocator={S8} reason={S8} function='{S8}' opcode={S8} operation={S8} verifier={S8} error={S8} ({u32}) block={u32} instruction={u32} operand={u32}"),
                diagnostic.code, backend.target, backend.allocator, backend.reason.length ? backend.reason : S8("not-applicable"),
                backend.function, backend.opcode, backend.operation,
                code.failed_machine_scheduled ? S8("scheduled-mir") : S8("selected-mir"),
                machine_verify_error_name(code.failed_machine_verification.error),
                (u32)code.failed_machine_verification.error, code.failed_machine_verification.block,
                code.failed_machine_verification.instruction, code.failed_machine_verification.operand)
            : string_format(arena,
                S8("C code generation refused: kind={S8} target={S8} allocator={S8} reason={S8} function='{S8}' opcode={S8} operation={S8}"),
                diagnostic.code, backend.target, backend.allocator, backend.reason.length ? backend.reason : S8("not-applicable"),
                backend.function, backend.opcode, backend.operation);
        compiler_driver_collect_diagnostic(warnings, diagnostic);
        result.error = COMPILER_DRIVER_ERROR_CODEGEN;
        result.diagnostic = compiler_diagnostic_render(arena, diagnostic);
        goto end;
    }
    if (invocation.reject_machine_fallback && code.statistics.fallback_function_count)
    {
        CodegenModule context = code;
        context.failed_function = code.first_fallback_function;
        context.failed_instruction = IR_INSTRUCTION_ID_INVALID;
        context.failed_opcode = code.first_fallback_opcode;
        CompilerDiagnosticBackend backend = compiler_driver_backend_context(arena, invocation, lowered.program, module, context);
        backend.reason = codegen_fallback_reason_string(code.first_fallback_reason);
        CompilerDiagnostic diagnostic = {
            .code = S8("codegen.machine-fallback"), .backend = &backend,
            .primary = compiler_driver_backend_location(lowered.program, module, context.failed_function, context.failed_instruction),
        };
        diagnostic.message = string_format(arena,
            S8("machine fallback rejected: target={S8} allocator={S8} reason={S8} opcode={S8} function='{S8}' fallbacks={u32}"),
            backend.target, backend.allocator, backend.reason, backend.opcode, backend.function, code.statistics.fallback_function_count);
        compiler_driver_collect_diagnostic(warnings, diagnostic);
        result.error = COMPILER_DRIVER_ERROR_CODEGEN;
        result.diagnostic = compiler_diagnostic_render(arena, diagnostic);
        goto end;
    }
    WORK_LEDGER_PHASE(OBJECT);
    compiler_driver_phase_begin(metrics, COMPILER_DRIVER_PHASE_OBJECT);
    ObjectFile object = object_from_canonical_codegen_module(arena, lowered.program, &code, invocation.target);
    result.object_error = object.error;
    if (object.error != OBJECT_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_OBJECT;
        result.diagnostic = object.error == OBJECT_ERROR_DEBUG_INFO ? S8("CodeView debug information exceeds record or section format limits")
                            : object.diagnostic.length              ? string_format(arena, S8("C object generation failed: {S8}"), object.diagnostic)
                                                                    : string_format(arena, S8("C object generation failed with error {u32}"), (u32)object.error);
        goto end;
    }
    result.object = object;
    result.has_object = true;
    if (metrics)
    {
        metrics->function_symbol_limit = code.entry_count;
    }
    WORK_LEDGER_PHASE(OUTPUT);
    compiler_driver_emit_object_output(arena, invocation, object, suppress_object_write, &result, metrics);
    if (investigation && result.error == COMPILER_DRIVER_ERROR_NONE)
    {
        compiler_driver_finish_investigation(arena, invocation, investigation, &object, &result);
    }
end:
    if (result.has_object)
    {
        compiler_driver_detach_object(arena, &preprocess, &result.object);
    }
    c_preprocess_release(&preprocess);
    file_map_unmap(source_file);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_object_path(Arena* arena, String8 input)
{
    u64 name = 0;
    u64 extension = input.length;
    for (u64 index = input.length; index != 0; index -= 1)
    {
        char8 byte = input.pointer[index - 1];
        if (byte == '.' && extension == input.length)
        {
            extension = index - 1;
        }
        if (byte == '/' || byte == '\\')
        {
            name = index;
            break;
        }
    }
    return string_format_z(arena, S8("{S8}.o"), (String8){
                                                           .pointer = input.pointer + name,
                                                           .length = extension - name,
                                                       });
}


BUSTER_GLOBAL_LOCAL GpuPipelineAction compiler_driver_gpu_action(CompilerDriverAction action)
{
    switch (action)
    {
    case COMPILER_DRIVER_ACTION_LINK: return GPU_PIPELINE_ACTION_LINK;
    case COMPILER_DRIVER_ACTION_PREPROCESS: return GPU_PIPELINE_ACTION_PREPROCESS;
    case COMPILER_DRIVER_ACTION_ASSEMBLY: return GPU_PIPELINE_ACTION_ASSEMBLY;
    case COMPILER_DRIVER_ACTION_OBJECT: return GPU_PIPELINE_ACTION_OBJECT;
    case COMPILER_DRIVER_ACTION_SYNTAX_ONLY: return GPU_PIPELINE_ACTION_SYNTAX_ONLY;
    case COMPILER_DRIVER_ACTION_COUNT: break;
    }
    return GPU_PIPELINE_ACTION_COUNT;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_gpu(Arena* arena, CompilerDriverInvocation invocation,
                                                                      CompilerDriverDiagnosticCollector* warnings)
{
    CompilerDriverResult result = {0};
    GpuSourceLanguage language = compiler_driver_gpu_language(invocation.language);
    GpuSourceLanguage* input_languages = 0;
    if (invocation.input_languages)
    {
        input_languages = arena_allocate(arena, GpuSourceLanguage, invocation.input_count);
        for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
        {
            input_languages[input_index] = compiler_driver_gpu_language(compiler_driver_input_language(invocation, input_index));
            if (input_languages[input_index] == GPU_SOURCE_LANGUAGE_COUNT)
            {
                result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
                result.diagnostic = S8("invalid per-input language for GPU compilation");
                return result;
            }
        }
    }
    GpuPipelineAction action = compiler_driver_gpu_action(invocation.action);
    if ((!input_languages && language == GPU_SOURCE_LANGUAGE_COUNT) || action == GPU_PIPELINE_ACTION_COUNT)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("invalid language or action for GPU compilation");
        return result;
    }
    bool capture_text_output = !invocation.output_path.length &&
                               (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS || invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY);
    GpuPipelineResult pipeline = gpu_pipeline_execute(arena,
                                                      (GpuPipelineOptions){
                                                          .input_paths = invocation.input_paths,
                                                          .input_languages = input_languages,
                                                          .include_paths = invocation.include_paths,
                                                          .system_include_paths = invocation.system_include_paths,
                                                          .macro_operations = invocation.macro_operations,
                                                          .definitions = invocation.definitions,
                                                          .undefinitions = invocation.undefinitions,
                                                          .extra_arguments = invocation.gpu_arguments,
                                                          .output_path = invocation.output_path,
                                                          .sysroot = invocation.sysroot,
                                                          .cuda_path = invocation.cuda_path,
                                                          .rocm_path = invocation.rocm_path,
                                                          .tools = invocation.gpu_tools,
                                                          .target = invocation.gpu_target,
                                                          .input_count = invocation.input_count,
                                                          .include_path_count = invocation.include_path_count,
                                                          .system_include_path_count = invocation.system_include_path_count,
                                                          .macro_operation_count = invocation.macro_operation_count,
                                                          .definition_count = invocation.definition_count,
                                                          .undefinition_count = invocation.undefinition_count,
                                                          .extra_argument_count = invocation.gpu_argument_count,
                                                          .language = language,
                                                          .action = action,
                                                          .optimization_level = invocation.optimization_level,
                                                          .debug_info = invocation.debug_info,
                                                          .no_standard_includes = invocation.no_standard_includes,
                                                          .save_temporaries = invocation.save_gpu_temporaries,
                                                          .capture_text_output = capture_text_output,
                                                      });
    if (pipeline.published)
    {
        result.gpu = pipeline.artifact;
        result.has_gpu = true;
    }
    if (pipeline.error != GPU_PIPELINE_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_GPU;
        result.diagnostic = pipeline.diagnostic.length ? pipeline.diagnostic : S8("GPU pipeline failed without a diagnostic");
        if (invocation.save_gpu_temporaries && pipeline.temporary_directory.length)
        {
            result.diagnostic = string_format(arena, S8("{S8}\nGPU temporary files: {S8}"), result.diagnostic, pipeline.temporary_directory);
        }
        return result;
    }
    if (pipeline.log.length)
    {
        compiler_driver_warning_append_text(warnings, pipeline.log);
    }
    if (pipeline.log_truncated)
    {
        compiler_driver_warning_append_text(warnings, string_format(arena, S8("GPU tool output truncated: {u64} bytes not retained\n"), pipeline.log_dropped_bytes));
    }
    if (invocation.save_gpu_temporaries && pipeline.temporary_directory.length)
    {
        compiler_driver_warning_append_text(warnings, string_format(arena, S8("GPU temporary files: {S8}\n"), pipeline.temporary_directory));
    }
    if (pipeline.artifact.format != GPU_OUTPUT_NONE)
    {
        result.gpu = pipeline.artifact;
        result.has_gpu = true;
        if (capture_text_output)
        {
            result.output = BYTE_SLICE_TO_STRING(8, pipeline.artifact.bytes);
        }
    }
    return result;
}

// Every translation unit enters here, so a requested measurement brackets
// the complete compiler_driver_execute_c_single call, including its assembly
// dispatch and early exits.
BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_unit(Arena* arena, CompilerDriverInvocation invocation, bool suppress_object_write,
                                                                      CompilerDriverDiagnosticCollector* warnings, CompilerDriverUnitMetrics* metrics)
{
    if (metrics)
    {
        *metrics = (CompilerDriverUnitMetrics){.origin = invocation.metrics_origin, .active = COMPILER_DRIVER_PHASE_READ};
#if BUSTER_INCLUDE_TESTS
        compiler_driver_test_setup_order_event(COMPILER_DRIVER_TEST_SETUP_INPUT_BEGIN);
#endif
        metrics->start_nanoseconds = timestamp_ns_between(metrics->origin, timestamp_take());
        metrics->last_nanoseconds = metrics->start_nanoseconds;
    }
    CompilerDriverResult result = compiler_driver_execute_c_single(arena, invocation, suppress_object_write, warnings, metrics);
    if (metrics)
    {
        compiler_driver_phase_begin(metrics, metrics->active);
#if BUSTER_INCLUDE_TESTS
        compiler_driver_test_setup_order_event(COMPILER_DRIVER_TEST_SETUP_INPUT_END);
#endif
    }
    return result;
}

// The memory one unit needs, through the scoped Arena.high_water mark on its
// TU arena and the running thread's scratch arenas: each arena's peak above
// its position when the unit started, rounded up to that arena's commit
// granularity, summed. Ending the watch restores each enclosing observer's
// mark (a test arena scope) raised by this unit's peak.
#define COMPILER_DRIVER_WATCHED_ARENAS (1 + (u32)SCRATCH_ARENA_COUNT)
typedef struct CompilerDriverArenaWatch CompilerDriverArenaWatch;
struct CompilerDriverArenaWatch
{
    Arena* arenas[COMPILER_DRIVER_WATCHED_ARENAS];
    u64 starts[COMPILER_DRIVER_WATCHED_ARENAS];
    u64 saved_high_water[COMPILER_DRIVER_WATCHED_ARENAS];
};

BUSTER_GLOBAL_LOCAL CompilerDriverArenaWatch compiler_driver_arena_watch_begin(Arena* unit_arena)
{
    CompilerDriverArenaWatch result = {.arenas = {unit_arena}};
    ThreadContext* context = thread_context_selected();
    for (u32 slot = 0; slot < (u32)SCRATCH_ARENA_COUNT && context; slot += 1)
    {
        result.arenas[1 + slot] = context->arenas[slot] != unit_arena ? context->arenas[slot] : 0;
    }
    for (u32 index = 0; index < COMPILER_DRIVER_WATCHED_ARENAS; index += 1)
    {
        Arena* arena = result.arenas[index];
        if (arena)
        {
            result.starts[index] = arena->position;
            result.saved_high_water[index] = arena->high_water;
            arena->high_water = arena->position;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_arena_watch_end(CompilerDriverArenaWatch* watch, CompilerDriverUnitMetrics* metrics)
{
    for (u32 index = 0; index < COMPILER_DRIVER_WATCHED_ARENAS; index += 1)
    {
        Arena* arena = watch->arenas[index];
        if (arena)
        {
            u64 peak = BUSTER_MAX(arena->high_water, arena->position);
            metrics->arena_peak_bytes += align_forward(peak - watch->starts[index], arena->granularity);
            arena->high_water = BUSTER_MAX(watch->saved_high_water[index], peak);
        }
    }
    metrics->arena_retained_bytes = watch->arenas[0] ? watch->arenas[0]->position - watch->starts[0] : 0;
    *watch = (CompilerDriverArenaWatch){0};
}

BUSTER_GLOBAL_LOCAL CompilerDriverInputStatus compiler_driver_input_status(CompilerDriverError error)
{
    CompilerDriverInputStatus result;
    switch (error)
    {
    case COMPILER_DRIVER_ERROR_NONE:
        result = COMPILER_DRIVER_INPUT_STATUS_OK;
        break;
    case COMPILER_DRIVER_ERROR_INVALID_INPUT:
    case COMPILER_DRIVER_ERROR_TOKENIZE:
    case COMPILER_DRIVER_ERROR_PARSE:
    case COMPILER_DRIVER_ERROR_ANALYSIS:
        result = COMPILER_DRIVER_INPUT_STATUS_REJECTED;
        break;
    default:
        result = COMPILER_DRIVER_INPUT_STATUS_FAILED;
        break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL const u8 compiler_driver_section_classes[] = {
    [OBJECT_SECTION_TEXT] = COMPILER_DRIVER_SECTION_CLASS_TEXT,
    [OBJECT_SECTION_READ_ONLY_DATA] = COMPILER_DRIVER_SECTION_CLASS_READ_ONLY_DATA,
    [OBJECT_SECTION_DATA] = COMPILER_DRIVER_SECTION_CLASS_DATA,
    [OBJECT_SECTION_ZERO] = COMPILER_DRIVER_SECTION_CLASS_ZERO,
    [OBJECT_SECTION_THREAD_LOCAL_DATA] = COMPILER_DRIVER_SECTION_CLASS_THREAD_LOCAL_DATA,
    [OBJECT_SECTION_THREAD_LOCAL_ZERO] = COMPILER_DRIVER_SECTION_CLASS_THREAD_LOCAL_ZERO,
    [OBJECT_SECTION_INIT_ARRAY] = COMPILER_DRIVER_SECTION_CLASS_INITIALIZER,
    [OBJECT_SECTION_FINI_ARRAY] = COMPILER_DRIVER_SECTION_CLASS_INITIALIZER,
    [OBJECT_SECTION_UNWIND] = COMPILER_DRIVER_SECTION_CLASS_UNWIND,
    [OBJECT_SECTION_WINDOWS_PDATA] = COMPILER_DRIVER_SECTION_CLASS_UNWIND,
    [OBJECT_SECTION_WINDOWS_XDATA] = COMPILER_DRIVER_SECTION_CLASS_UNWIND,
    [OBJECT_SECTION_DEBUG_INFO] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_ABBREV] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_LINE] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_STR] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_LOC] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_RANGES] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_CODEVIEW_TYPES] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_ADDR] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_STR_OFFSETS] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_LINE_STR] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_RNGLISTS] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
    [OBJECT_SECTION_DEBUG_LOCLISTS] = COMPILER_DRIVER_SECTION_CLASS_DEBUG,
};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(compiler_driver_section_classes) == OBJECT_SECTION_COUNT);

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL u32 compiler_driver_function_limit_override;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool compiler_driver_setup_order_armed;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL CompilerDriverTestSetupOrder compiler_driver_setup_order;

void compiler_driver_test_setup_order_begin(void)
{
    compiler_driver_setup_order = (CompilerDriverTestSetupOrder){0};
    compiler_driver_setup_order_armed = true;
}

void compiler_driver_test_setup_order_event(CompilerDriverTestSetupEvent event)
{
    if (compiler_driver_setup_order_armed)
    {
        CompilerDriverTestSetupOrder* order = &compiler_driver_setup_order;
        switch (event)
        {
        case COMPILER_DRIVER_TEST_SETUP_COMPILER:
        case COMPILER_DRIVER_TEST_SETUP_TARGET:
        case COMPILER_DRIVER_TEST_SETUP_ARENAS:
            order->order_errors += (u32)(order->input_starts != 0 || order->input_open || (order->completed_setup & (u32)event) != 0);
            order->completed_setup |= (u32)event;
            break;
        case COMPILER_DRIVER_TEST_SETUP_INPUT_BEGIN:
            order->order_errors += (u32)(order->completed_setup != BUSTER_COMPILER_DRIVER_TEST_SETUP_COMPLETE || order->input_open);
            order->input_starts += 1;
            order->input_open = true;
            break;
        case COMPILER_DRIVER_TEST_SETUP_INPUT_END:
            order->order_errors += (u32)!order->input_open;
            order->input_ends += 1;
            order->input_open = false;
            break;
        default:
            order->order_errors += 1;
            break;
        }
    }
}

CompilerDriverTestSetupOrder compiler_driver_test_setup_order_end(void)
{
    if (compiler_driver_setup_order_armed)
    {
        compiler_driver_setup_order.order_errors += (u32)(compiler_driver_setup_order.input_open ||
                                                           compiler_driver_setup_order.input_starts != compiler_driver_setup_order.input_ends);
    }
    CompilerDriverTestSetupOrder result = compiler_driver_setup_order;
    compiler_driver_setup_order = (CompilerDriverTestSetupOrder){0};
    compiler_driver_setup_order_armed = false;
    return result;
}

void compiler_driver_test_set_function_limit(u32 limit)
{
    compiler_driver_function_limit_override = limit;
}
#endif

BUSTER_GLOBAL_LOCAL u32 compiler_driver_function_limit(void)
{
    u32 result = COMPILER_DRIVER_INPUT_FUNCTION_LIMIT;
#if BUSTER_INCLUDE_TESTS
    result = compiler_driver_function_limit_override ? compiler_driver_function_limit_override : result;
#endif
    return result;
}

// Little-endian framing for the diagnostic digest, so equal records hash
// equally on every host.
BUSTER_GLOBAL_LOCAL void compiler_driver_digest_u64(Sha256* hash, u64 value)
{
    u8 bytes[8];
    for (u32 index = 0; index < 8; index += 1)
    {
        bytes[index] = (u8)(value >> (index * 8));
    }
    sha256_add(hash, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL void compiler_driver_digest_text(Sha256* hash, String8 text)
{
    compiler_driver_digest_u64(hash, text.length);
    sha256_add(hash, text.pointer, text.length);
}

// Publishes one compiled unit into its input record while the unit arena is
// still alive: names and the first error are copied into the result arena.
// `first_record` is where this unit's structured diagnostics begin in the
// result collector.
BUSTER_GLOBAL_LOCAL void compiler_driver_input_record(Arena* arena, CompilerDriverInvocation const* invocation, CompilerDriverInputResult* record,
                                                      CompilerDriverResult const* unit, CompilerDriverDiagnosticCollector const* collector,
                                                      u32 first_record, CompilerDriverUnitMetrics const* metrics)
{
    record->error = unit->error;
    record->status = compiler_driver_input_status(unit->error);
    record->message = unit->error != COMPILER_DRIVER_ERROR_NONE ? string_duplicate_arena(arena, unit->diagnostic, false) : (String8){0};
    bool first_error = false;
    for (u32 index = first_record; index < collector->record_count; index += 1)
    {
        CompilerDiagnostic const* diagnostic = &collector->records[index];
        if (diagnostic->severity == COMPILER_DIAGNOSTIC_ERROR)
        {
            record->error_count += 1;
            if (!first_error)
            {
                first_error = true;
                record->diagnostic_code = diagnostic->code;
                record->diagnostic_path = diagnostic->primary.path;
                record->diagnostic_line = diagnostic->primary.position.line;
                record->diagnostic_column = diagnostic->primary.position.column;
            }
        }
        else if (diagnostic->severity == COMPILER_DIAGNOSTIC_WARNING)
        {
            record->warning_count += 1;
        }
    }
    if (unit->error != COMPILER_DRIVER_ERROR_NONE && !first_error)
    {
        record->diagnostic_code = compiler_driver_error_code(unit->error);
    }
    if (metrics)
    {
        Sha256 hash;
        sha256_init(&hash);
        for (u32 index = first_record; index < collector->record_count; index += 1)
        {
            CompilerDiagnostic const* diagnostic = &collector->records[index];
            compiler_driver_digest_u64(&hash, (u64)diagnostic->severity);
            compiler_driver_digest_text(&hash, diagnostic->code);
            compiler_driver_digest_text(&hash, diagnostic->primary.path);
            compiler_driver_digest_u64(&hash, diagnostic->primary.position.line);
            compiler_driver_digest_u64(&hash, diagnostic->primary.position.column);
            compiler_driver_digest_text(&hash, diagnostic->message);
        }
        char8* digest = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
        sha256_finish_hex(&hash, digest);
        record->diagnostic_digest = (String8){.pointer = digest, .length = SHA256_HEX_CAPACITY - 1};
        record->diagnostic_record_count = collector->record_count - first_record;
        record->measured = true;
        record->start_nanoseconds = metrics->start_nanoseconds;
        record->end_nanoseconds = metrics->last_nanoseconds;
        memcpy(record->phase_nanoseconds, metrics->phase_nanoseconds, sizeof(record->phase_nanoseconds));
        record->arena_peak_bytes = metrics->arena_peak_bytes;
        record->arena_retained_bytes = metrics->arena_retained_bytes;
        record->object_file_bytes = metrics->object_file_bytes;
        record->codegen = unit->codegen_statistics;
        record->fallback_record_count = unit->fallback_record_count;
        record->source_bytes = unit->source_lexed.translated_bytes;
        record->preprocessed_tokens = unit->preprocessed.tokens;
        if (unit->has_object)
        {
            ObjectFile const* object = &unit->object;
            for (u32 section_index = 0; section_index < object->section_count; section_index += 1)
            {
                ObjectSection const* section = &object->sections[section_index];
                u64 bytes = object_section_kind_is_zero_fill(section->kind) ? section->virtual_size : section->data.length;
                record->section_bytes[(u32)section->kind < OBJECT_SECTION_COUNT ? compiler_driver_section_classes[section->kind] : COMPILER_DRIVER_SECTION_CLASS_DEBUG] += bytes;
            }
            if (invocation->collect_function_sizes)
            {
                u32 limit = BUSTER_MIN(metrics->function_symbol_limit, object->symbol_count);
                u32 defined = 0;
                for (u32 symbol_index = 0; symbol_index < limit; symbol_index += 1)
                {
                    ObjectSymbol const* symbol = &object->symbols[symbol_index];
                    defined += (u32)(symbol->kind == OBJECT_SYMBOL_FUNCTION && symbol->section < object->section_count);
                }
                u32 function_limit = compiler_driver_function_limit();
                u32 retained = BUSTER_MIN(defined, function_limit);
                record->functions = arena_allocate(arena, CompilerDriverFunctionSize, retained);
                record->functions_omitted = defined - retained;
                for (u32 symbol_index = 0; symbol_index < limit && record->function_count < retained; symbol_index += 1)
                {
                    ObjectSymbol const* symbol = &object->symbols[symbol_index];
                    if (symbol->kind == OBJECT_SYMBOL_FUNCTION && symbol->section < object->section_count)
                    {
                        record->functions[record->function_count] = (CompilerDriverFunctionSize){
                            .name = string_duplicate_arena(arena, symbol->name, false),
                            .code_bytes = symbol->size,
                        };
                        record->function_count += 1;
                    }
                }
            }
        }
    }
}

// Multi-input frontend/IR state is retained for at most one worker-sized
// batch and copied into the result arena in input order. A single source that
// continues through the general linker instead uses the result arena directly,
// so its object payload can remain live without a deep copy.
typedef struct CompilerDriverUnit CompilerDriverUnit;
struct CompilerDriverUnit
{
    Arena* arena;
    CompilerDriverDiagnosticCollector warnings;
    CompilerDriverResult result;
    // Filled only when the batch collects input metrics.
    CompilerDriverUnitMetrics metrics;
};

typedef struct CompilerDriverUnitBatch CompilerDriverUnitBatch;
struct CompilerDriverUnitBatch
{
    CompilerDriverInvocation invocation;
    CompilerDriverUnit* units;
    u32 first_input;
    u32 count;
    u32 workers;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_parallel_c_input(CompilerDriverInvocation invocation, u32 input_index)
{
    String8 path = invocation.input_paths[input_index];
    CompilerDriverLanguage language = compiler_driver_input_language(invocation, input_index);
    bool native = invocation.target.cpu_arch == CPU_ARCH_X86_64 || invocation.target.cpu_arch == CPU_ARCH_AARCH64;
    return native && invocation.action == COMPILER_DRIVER_ACTION_LINK && !invocation.emit_llvm_bitcode &&
           !invocation.has_gpu_target && !compiler_driver_object_input(path) && !compiler_driver_archive_input(path) &&
           compiler_driver_c_input(language, path) &&
           !compiler_driver_assembly_input(language, path) && !compiler_driver_preprocessed_assembly_input(path);
}

BUSTER_GLOBAL_LOCAL u32 compiler_driver_unit_worker_limit(CompilerDriverInvocation invocation)
{
    u32 result = 1;
#if !BUSTER_SINGLE_THREADED
    // An embedding caller's existing gang already owns its parallel budget.
    // Do not turn each of its invocations into another gang.
    if (invocation.compile_jobs > 1 && !invocation.source_cache && lane_count() == 1)
    {
        u32 logical = BUSTER_MAX(os_get_logical_thread_count(), (u32)1);
        result = BUSTER_MAX((u32)1, BUSTER_MIN(invocation.compile_jobs, BUSTER_MIN(logical, invocation.input_count)));
    }
#else
    BUSTER_UNUSED(invocation);
#endif
    return result;
}

void compiler_parallel_prewarm(void)
{
    compiler_prewarm();
    // A later invocation may change targets while our persistent gang is
    // parked. Prepare both native families before the first worker exists;
    // this opt-in cold cost must not leak into ordinary serial compilation.
    codegen_prewarm_for_target((Target){.cpu_arch = CPU_ARCH_X86_64});
    machine_x86_64_exact_prewarm_all_shapes();
    buster_x86_metadata_prewarm_all_forms();
    buster_aarch64_prewarm();
    buster_aarch64_semantics_prewarm();
}

BUSTER_GLOBAL_LOCAL ThreadReturnType compiler_driver_unit_lane(void* argument)
{
    CompilerDriverUnitBatch* batch = (CompilerDriverUnitBatch*)argument;
    if (lane_index() == 0)
    {
        batch->workers = (u32)lane_count();
    }
    LaneRange range = lane_range(batch->count);
    for (u64 index = range.start; index < range.end; index += 1)
    {
        // The coordinator created this slot's arena and will destroy it; the
        // lane only fills it.
        CompilerDriverUnit* unit = &batch->units[index];
        if (unit->arena)
        {
            unit->warnings = (CompilerDriverDiagnosticCollector){
                .arena = unit->arena,
                .suppress_records = batch->invocation.suppress_diagnostic_records,
                .suppress_warnings = batch->invocation.suppress_warnings,
            };
            CompilerDriverInvocation single = batch->invocation;
            single.input_paths += batch->first_input + index;
            if (single.input_languages)
            {
                single.input_languages += batch->first_input + index;
            }
            single.input_count = 1;
            single.input_language_count = single.input_languages ? 1 : 0;
            single.link_operations = 0;
            single.link_operation_count = 0;
            single.output_path = (String8){0};
            single.action = COMPILER_DRIVER_ACTION_OBJECT;
            bool measure = batch->invocation.collect_input_metrics;
            CompilerDriverArenaWatch watch = {0};
            if (measure)
            {
                watch = compiler_driver_arena_watch_begin(unit->arena);
            }
            unit->result = compiler_driver_execute_unit(unit->arena, single, true, &unit->warnings, measure ? &unit->metrics : 0);
            if (measure)
            {
                compiler_driver_arena_watch_end(&watch, &unit->metrics);
            }
        }
        // A failed arena remains a null slot. The caller diagnoses allocation
        // and all other failures in input order, not worker completion order.
    }
}

// First-touch page faults are setup too: a fresh TU arena mapping and the
// thread's scratch arenas fault their pages in the first unit and are reused
// by every later one. Priming commits and touches a bounded prefix of each
// (a pooled TU arena that the first unit's arena_create takes back, and the
// scratch arenas) once per thread, and of the result arena when a single
// input compiles in it, before the first interval. The pages stay committed
// (pooling and scratch rewinds never decommit), so later invocations on the
// same thread skip it. Lane workers' own arenas are not primed.
#define COMPILER_DRIVER_METRICS_TU_PRIME_BYTES BUSTER_MB(4)
#define COMPILER_DRIVER_METRICS_SCRATCH_PRIME_BYTES BUSTER_MB(1)

BUSTER_GLOBAL_LOCAL void compiler_driver_prime_arena(Arena* arena, u64 bytes)
{
    u64 position = arena->position;
    u64 size = BUSTER_MIN(bytes, arena->reserved_size - position);
    memset(arena_allocate(arena, u8, size), 0, size);
    arena_set_position(arena, position);
}

BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool compiler_driver_thread_arenas_primed;

BUSTER_GLOBAL_LOCAL void compiler_driver_prime_arenas(Arena* result_arena, bool result_arena_is_unit)
{
    if (!compiler_driver_thread_arenas_primed)
    {
        Arena* pooled = arena_create((ArenaCreation){
            .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
            .flags = {.pool_reuse = 1},
        });
        if (pooled)
        {
            compiler_driver_prime_arena(pooled, COMPILER_DRIVER_METRICS_TU_PRIME_BYTES);
            arena_destroy(pooled, 1);
        }
        ThreadContext* context = thread_context_selected();
        for (u32 slot = 0; slot < (u32)SCRATCH_ARENA_COUNT && context; slot += 1)
        {
            if (context->arenas[slot] && context->arenas[slot] != result_arena)
            {
                compiler_driver_prime_arena(context->arenas[slot], COMPILER_DRIVER_METRICS_SCRATCH_PRIME_BYTES);
            }
        }
        compiler_driver_thread_arenas_primed = true;
    }
    if (result_arena_is_unit)
    {
        compiler_driver_prime_arena(result_arena, COMPILER_DRIVER_METRICS_TU_PRIME_BYTES);
    }
}

// A failure the driver finds outside a compiled unit (an unreadable
// prebuilt input, a TU arena it could not allocate). It becomes that input's
// failed record and, unless an earlier -fkeep-going failure already named
// the invocation's error, the invocation's error.
BUSTER_GLOBAL_LOCAL void compiler_driver_fail_input(CompilerDriverResult* result, CompilerDriverInputResult* inputs, u32 input_index,
                                                       CompilerDriverError error, ObjectError object_error, String8 diagnostic)
{
    if (result->error == COMPILER_DRIVER_ERROR_NONE)
    {
        result->error = error;
        result->object_error = object_error;
        result->diagnostic = diagnostic;
    }
    if (inputs)
    {
        inputs[input_index].status = COMPILER_DRIVER_INPUT_STATUS_FAILED;
        inputs[input_index].error = error;
        inputs[input_index].message = diagnostic;
        inputs[input_index].diagnostic_code = compiler_driver_error_code(error);
    }
}

CompilerDriverResult compiler_driver_execute_invocation(Arena* arena, CompilerDriverInvocation invocation)
{
    CompilerDriverDiagnosticCollector warnings = {
        .arena = arena,
        .suppress_records = invocation.suppress_diagnostic_records,
        .suppress_warnings = invocation.suppress_warnings,
    };
    CompilerDriverResult result = {.compilation_workers = 1};
    CompilerDriverArchiveState archive_state = {0};
    FileMapRead* input_archive_maps = 0;
    FileMapRead* library_archive_maps = 0;
    u32 fallback_record_capacity = 0;
    CompilerDriverUnit* unit_tasks = 0;
    u32 unit_task_count = 0;
    CSourceCache* owned_source_cache = 0;
    // Per-input records live outside `result`, which several paths replace
    // wholesale, and are attached at `finish`.
    CompilerDriverInputResult* inputs = 0;
    bool measure = invocation.collect_input_metrics;
    if (measure && !invocation.has_metrics_origin)
    {
        invocation.metrics_origin = timestamp_take();
        invocation.has_metrics_origin = true;
    }
    if (!arena)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("compiler driver requires an arena");
        return result;
    }
    if (invocation.enable_source_cache && !invocation.source_cache)
    {
        owned_source_cache = c_source_cache_create(arena, COMPILER_DRIVER_SOURCE_CACHE_BYTE_LIMIT);
        invocation.source_cache = owned_source_cache;
    }
    if ((measure || invocation.keep_going) && invocation.input_count && invocation.error == COMPILER_DRIVER_ERROR_NONE)
    {
        inputs = arena_allocate(arena, CompilerDriverInputResult, invocation.input_count);
        memset(inputs, 0, sizeof(*inputs) * invocation.input_count);
        for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
        {
            inputs[input_index].index = input_index;
            inputs[input_index].path = string_duplicate_arena(arena, invocation.input_paths[input_index], false);
            inputs[input_index].status = COMPILER_DRIVER_INPUT_STATUS_NOT_RUN;
        }
    }
    if (invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        result.error = invocation.error;
        result.diagnostic = invocation.diagnostic.length ? invocation.diagnostic : S8("invalid compiler invocation");
        goto finish;
    }
    compiler_driver_validate_request(arena, &invocation);
    if (invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        result.error = invocation.error;
        result.diagnostic = invocation.diagnostic;
        goto finish;
    }
    // Count both explicit entries and the target's defaults before any source
    // read, fast-path allocation or lookup; export maps also reserve libc.
    u64 library_count = (u64)invocation.library_count + invocation.framework_count;
    u32 library_reserve = invocation.target.os == OPERATING_SYSTEM_WINDOWS ? 8u : invocation.target.os == OPERATING_SYSTEM_LINUX ? 2u : 0u;
    if (library_count >= (u64)UINT32_MAX - library_reserve || invocation.library_path_count > UINT32_MAX - 8u)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("native library counts exceed driver limits");
        goto finish;
    }
    // The digest covers every structured record, and function sizes are a
    // refinement of a measurement, so neither may be silently empty.
    if ((measure && invocation.suppress_diagnostic_records) || (invocation.collect_function_sizes && !measure))
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = measure ? S8("per-input metrics require structured diagnostic records")
                                    : S8("function sizes require per-input metrics");
        goto finish;
    }
    if (invocation.linker_argument_count)
    {
        String8 unsupported = invocation.linker_arguments ? invocation.linker_arguments[0] : S8("(missing argument storage)");
        NativeExecutableLinkOptions options = {
            .linker_arguments = invocation.linker_arguments,
            .linker_argument_count = invocation.linker_argument_count,
            .image_kind = (u8)invocation.image_kind,
        };
        if (invocation.action != COMPILER_DRIVER_ACTION_LINK || invocation.has_gpu_target || invocation.emit_llvm_bitcode ||
            !link_validate_linker_arguments(invocation.target, options, true, &unsupported))
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = string_format(arena, S8("unsupported linker argument for this output: {S8}"), unsupported);
            goto finish;
        }
    }
    if ((invocation.input_languages && invocation.input_language_count != invocation.input_count) ||
        (!invocation.input_languages && invocation.input_language_count))
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("per-input language selection count does not match input count");
        goto finish;
    }
    if (!compiler_driver_link_operations_valid(&invocation))
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("link operation stream does not cover inputs and libraries in order");
        goto finish;
    }
    compiler_driver_validate_spirv_invocation(&invocation);
    compiler_driver_validate_native_pic_invocation(arena, &invocation, (String8){0});
    if (invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        result.error = invocation.error;
        result.diagnostic = invocation.diagnostic;
        goto finish;
    }
    if (invocation.bootstrap_trace_prefix.length &&
        (invocation.input_count != 1 ||
         !compiler_driver_c_input(compiler_driver_input_language(invocation, 0), invocation.input_paths[0]) ||
         compiler_driver_assembly_input(compiler_driver_input_language(invocation, 0), invocation.input_paths[0]) ||
         invocation.has_gpu_target || invocation.emit_llvm_bitcode ||
         (invocation.target.cpu_arch != CPU_ARCH_X86_64 && invocation.target.cpu_arch != CPU_ARCH_AARCH64) ||
         (invocation.action != COMPILER_DRIVER_ACTION_LINK && invocation.action != COMPILER_DRIVER_ACTION_OBJECT)))
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("bootstrap traces require exactly one native C input and object or executable output");
        goto finish;
    }
    if (invocation.investigation_path.length || invocation.investigation_function.length)
    {
        bool supported = invocation.investigation_path.length && invocation.investigation_function.length &&
                         invocation.investigation_function.length <= INVESTIGATION_TEXT_LIMIT &&
                         invocation.investigation_path.length <= INVESTIGATION_TEXT_LIMIT &&
                         invocation.investigation_configuration.length &&
                         invocation.investigation_configuration.length <= INVESTIGATION_TEXT_LIMIT &&
                         invocation.input_count == 1 && invocation.input_paths && !invocation.library_count && !invocation.framework_count &&
                         compiler_driver_c_input(compiler_driver_input_language(invocation, 0), invocation.input_paths[0]) &&
                         !compiler_driver_assembly_input(compiler_driver_input_language(invocation, 0), invocation.input_paths[0]) &&
                         invocation.action == COMPILER_DRIVER_ACTION_OBJECT && !invocation.has_gpu_target && !invocation.emit_llvm_bitcode &&
                         invocation.target.cpu_arch == CPU_ARCH_X86_64 && invocation.target.os == OPERATING_SYSTEM_LINUX &&
                         (invocation.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST ||
                          invocation.register_allocator == CODEGEN_REGISTER_ALLOCATOR_MIR_STACK);
        if (!supported)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("investigation requires a bounded configuration, capture path, function name, and one x86-64 Linux C -c input with fast or mir-stack allocation");
            goto finish;
        }
        String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_object_path(arena, invocation.input_paths[0]);
        String8 capture_absolute = os_path_absolute(arena, invocation.investigation_path, false);
        String8 input_absolute = os_path_absolute(arena, invocation.input_paths[0], false);
        String8 output_absolute = os_path_absolute(arena, output, false);
        bool aliases = capture_absolute.length && (string_equal(capture_absolute, input_absolute) || string_equal(capture_absolute, output_absolute));
        if (aliases || string_equal(invocation.investigation_path, output) || string_equal(invocation.investigation_path, invocation.input_paths[0]))
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("investigation capture path aliases its input or object output");
            goto finish;
        }
    }
    if (string_equal(invocation.output_path, S8("-")))
    {
        bool text_output = !invocation.emit_llvm_bitcode &&
                           (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS || invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY);
        if (!text_output || invocation.input_count > 1)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = text_output ? S8("cannot specify -o with multiple input files")
                                            : S8("-o - is supported only for preprocessing and textual assembly output");
            goto finish;
        }
        // Embedding callers retain text; the CLI writes it to stdout. GPU text
        // follows its existing capture-output path through the same convention.
        invocation.output_path = (String8){0};
    }
    if (invocation.has_gpu_target)
    {
        result = compiler_driver_execute_gpu(arena, invocation, &warnings);
        goto finish;
    }
    if (invocation.emit_llvm_bitcode)
    {
        // COFF is an object format, not permission to substitute the Windows
        // variadic convention for native AArch64 UEFI's LP64/AAPCS64 contract.
        // Refuse before reading inputs or creating/truncating any output.
        if (invocation.target.cpu_arch == CPU_ARCH_AARCH64 && invocation.target.os == OPERATING_SYSTEM_UEFI)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("AArch64 UEFI LLVM bitcode output is unsupported: native UEFI requires LP64/AAPCS64");
            goto finish;
        }
        if (invocation.library_count || invocation.library_path_count || invocation.framework_count || invocation.framework_path_count ||
            invocation.linker_argument_count)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("LLVM bitcode output does not accept native libraries, frameworks, or linker arguments");
            goto finish;
        }
        for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
        {
            if (compiler_driver_object_input(invocation.input_paths[input_index]) || compiler_driver_archive_input(invocation.input_paths[input_index]))
            {
                result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
                result.diagnostic = S8("native objects and archives cannot be included in LLVM bitcode output");
                goto finish;
            }
        }
    }
    if (invocation.target.cpu_arch == CPU_ARCH_WASM32 && invocation.emit_llvm_bitcode)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("-emit-llvm does not support wasm32-wasip1; use direct WebAssembly module output");
        goto finish;
    }
    if (compiler_driver_target_is_wasm(invocation.target))
    {
        if (!compiler_driver_target_is_supported_wasm(invocation.target))
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("direct WebAssembly output supports wasm32-wasip1 and wasm64-unknown-freestanding");
            goto finish;
        }
        if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("-S is not supported for direct WebAssembly module output");
            goto finish;
        }
        if (invocation.input_count > 1 || invocation.library_count || invocation.library_path_count || invocation.framework_count ||
            invocation.framework_path_count || invocation.linker_argument_count)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("WebAssembly accepts one source program and no native objects, archives, libraries, frameworks, or linker arguments");
            goto finish;
        }
        if (invocation.input_count &&
            (compiler_driver_object_input(invocation.input_paths[0]) || compiler_driver_archive_input(invocation.input_paths[0])))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("native objects and archives cannot be linked into a WebAssembly module");
            goto finish;
        }
    }
    else if (invocation.target.os == OPERATING_SYSTEM_WASI)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("WASI Preview 1 requires the wasm32-wasip1 target");
        goto finish;
    }
    if (invocation.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        if (invocation.target.os != OPERATING_SYSTEM_LINUX)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("eBPF currently requires the bpfel-unknown-linux target");
            goto finish;
        }
        if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("-S is not supported for direct eBPF object output");
            goto finish;
        }
        if (invocation.input_count > 1 || invocation.library_count || invocation.library_path_count || invocation.framework_count ||
            invocation.framework_path_count || invocation.linker_argument_count)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("eBPF accepts one source program and no native objects, archives, libraries, frameworks, or linker arguments");
            goto finish;
        }
        if (invocation.input_count &&
            (compiler_driver_object_input(invocation.input_paths[0]) || compiler_driver_archive_input(invocation.input_paths[0])))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("native objects and archives cannot be linked into an eBPF object");
            goto finish;
        }
    }
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 path = invocation.input_paths[input_index];
        CompilerDriverLanguage language = compiler_driver_input_language(invocation, input_index);
        bool object_input = compiler_driver_object_input(path);
        bool archive_input = compiler_driver_archive_input(path);
        if ((object_input || archive_input) && invocation.action != COMPILER_DRIVER_ACTION_LINK)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("prebuilt input {S8} is only valid while linking"), path);
            goto finish;
        }
        if (!object_input && !archive_input && !compiler_driver_c_input(language, path) && !compiler_driver_assembly_input(language, path) &&
            !compiler_driver_preprocessed_assembly_input(path))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("unsupported C input {S8}"), path);
            goto finish;
        }
    }
    if (measure)
    {
        // One-time table preparation is driver setup, not the first input's
        // work: fill it before any measured interval opens.
        compiler_prewarm();
#if BUSTER_INCLUDE_TESTS
        compiler_driver_test_setup_order_event(COMPILER_DRIVER_TEST_SETUP_COMPILER);
#endif
        codegen_prewarm_for_target(invocation.target);
#if BUSTER_INCLUDE_TESTS
        compiler_driver_test_setup_order_event(COMPILER_DRIVER_TEST_SETUP_TARGET);
#endif
        compiler_driver_prime_arenas(arena, invocation.input_count == 1);
#if BUSTER_INCLUDE_TESTS
        compiler_driver_test_setup_order_event(COMPILER_DRIVER_TEST_SETUP_ARENAS);
#endif
    }
    if (invocation.input_count <= 1 && !invocation.library_count &&
        (!invocation.input_count || (!compiler_driver_object_input(invocation.input_paths[0]) && !compiler_driver_archive_input(invocation.input_paths[0]))))
    {
        CompilerDriverUnitMetrics single_metrics;
        CompilerDriverArenaWatch watch = {0};
        u32 first_record = warnings.record_count;
        if (inputs && measure)
        {
            watch = compiler_driver_arena_watch_begin(arena);
        }
        result = compiler_driver_execute_unit(arena, invocation, false, &warnings, inputs && measure ? &single_metrics : 0);
        if (inputs)
        {
            if (measure)
            {
                compiler_driver_arena_watch_end(&watch, &single_metrics);
            }
            compiler_driver_input_record(arena, &invocation, &inputs[0], &result, &warnings, first_record, measure ? &single_metrics : 0);
        }
        goto finish;
    }
    if ((invocation.emit_llvm_bitcode || invocation.action == COMPILER_DRIVER_ACTION_OBJECT ||
         invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY || invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS ||
         invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY) &&
        invocation.output_path.length)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = invocation.emit_llvm_bitcode                         ? S8("cannot specify -o with -emit-llvm and multiple input files")
                             : invocation.action == COMPILER_DRIVER_ACTION_OBJECT ? S8("cannot specify -o with -c and multiple input files")
                             : invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ? S8("cannot specify -o with -S and multiple input files")
                             : invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS ? S8("cannot specify -o with -E and multiple input files")
                                                                                     : S8("cannot specify -o with -fsyntax-only and multiple input files");
        goto finish;
    }
    // Only the native link reads `objects`. A -c unit has already published
    // its own .o, and -S, -E, -fsyntax-only and -emit-llvm all finish before
    // link_objects, so no other action copies a unit's object out of its TU
    // arena or reserves a slot for it. Prebuilt inputs are link-only as well.
    bool link_inputs_retained = !invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_LINK;
    ObjectArchive* input_archives = arena_allocate(arena, ObjectArchive, invocation.input_count);
    input_archive_maps = arena_allocate_zeroed(arena, FileMapRead, invocation.input_count);
    u32 object_capacity = link_inputs_retained ? invocation.input_count : 0;
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 input_path = invocation.input_paths[input_index];
        if (!compiler_driver_archive_input(input_path))
        {
            continue;
        }
        FileMapRead archive_file = file_map_read(arena, input_path, (FileReadOptions){0});
        if (!archive_file.bytes.pointer)
        {
            compiler_driver_fail_input(&result, inputs, input_index, COMPILER_DRIVER_ERROR_FILE_READ, OBJECT_ERROR_NONE,
                                          string_format(arena, S8("could not read {S8}"), input_path));
            file_map_unmap(archive_file);
            goto finish;
        }
        input_archive_maps[input_index] = archive_file;
        input_archives[input_index] = object_archive_read_link(arena, archive_file.bytes, invocation.target);
        if (input_archives[input_index].error != OBJECT_ERROR_NONE || input_archives[input_index].object_count > UINT32_MAX - object_capacity)
        {
            compiler_driver_fail_input(&result, inputs, input_index, COMPILER_DRIVER_ERROR_OBJECT, input_archives[input_index].error,
                                          string_format(arena, S8("could not read archive {S8}: error {u32}"), input_path, (u32)input_archives[input_index].error));
            goto finish;
        }
        object_capacity += input_archives[input_index].object_count;
    }
    ObjectArchive* library_archives = arena_allocate(arena, ObjectArchive, invocation.library_count);
    library_archive_maps = arena_allocate_zeroed(arena, FileMapRead, invocation.library_count);
    String8* library_archive_paths = arena_allocate(arena, String8, invocation.library_count);
    bool* static_libraries = arena_allocate(arena, bool, invocation.library_count);
    memset(static_libraries, 0, sizeof(*static_libraries) * invocation.library_count);
    for (u32 library_index = 0; library_index < invocation.library_count; library_index += 1)
    {
        bool found = false;
        String8 archive_path = {0};
        ObjectArchive archive = compiler_driver_library_archive(arena, invocation, invocation.libraries[library_index], &found, &archive_path, &library_archive_maps[library_index]);
        if (!found)
        {
            if (invocation.target.os == OPERATING_SYSTEM_UEFI)
            {
                result.error = COMPILER_DRIVER_ERROR_FILE_READ;
                result.diagnostic = string_format(arena, S8("could not find static UEFI library {S8}"), invocation.libraries[library_index]);
                goto finish;
            }
            continue;
        }
        library_archive_paths[library_index] = archive_path;
        static_libraries[library_index] = true;
        library_archives[library_index] = archive;
        if (archive.error != OBJECT_ERROR_NONE || archive.object_count > UINT32_MAX - object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = archive.error;
            result.diagnostic = string_format(arena, S8("could not read archive {S8}: error {u32}"), archive_path, (u32)archive.error);
            goto finish;
        }
        object_capacity += archive.object_count;
    }
    u32 runtime_object_capacity = link_inputs_retained ? compiler_driver_runtime_object_capacity(invocation.target) : 0;
    if (runtime_object_capacity)
    {
        if (object_capacity > UINT32_MAX - runtime_object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("too many link inputs");
            goto finish;
        }
        object_capacity += runtime_object_capacity;
    }
    ObjectFile* objects = arena_allocate(arena, ObjectFile, object_capacity);
    String8* preprocessed = arena_allocate(arena, String8, invocation.input_count);
    u32 object_count = 0;
    u32 unit_task_capacity = compiler_driver_unit_worker_limit(invocation);
    u32 batch_first = 0;
    u32 batch_end = 0;
    bool units_prewarmed = false;
    bool ordered_link = invocation.action == COMPILER_DRIVER_ACTION_LINK && invocation.link_operation_count;
    u64 link_operation_count = ordered_link ? invocation.link_operation_count : (u64)invocation.input_count + invocation.library_count;
    for (u64 operation_index = 0; operation_index < link_operation_count; operation_index += 1)
    {
        CompilerDriverLinkOperation operation = compiler_driver_link_operation(&invocation, operation_index);
        if (operation.kind == COMPILER_DRIVER_LINK_OPERATION_LIBRARY)
        {
            u32 library_index = operation.index;
            if (static_libraries[library_index])
            {
                ObjectArchive* archive = &library_archives[library_index];
                compiler_driver_archive_extract(arena, &archive_state, archive, objects, &object_count);
                if (archive->error != OBJECT_ERROR_NONE)
                {
                    result.error = COMPILER_DRIVER_ERROR_OBJECT;
                    result.object_error = archive->error;
                    result.diagnostic = string_format(arena, S8("could not read archive {S8}: {S8}"), library_archive_paths[library_index], archive->diagnostic);
                    goto finish;
                }
            }
            continue;
        }
        u32 input_index = operation.index;
        String8 input_path = invocation.input_paths[input_index];
        if (inputs && (compiler_driver_archive_input(input_path) || compiler_driver_object_input(input_path)))
        {
            // Prebuilt link inputs are never compiled, and a malformed one
            // still stops the link even under -fkeep-going.
            inputs[input_index].status = COMPILER_DRIVER_INPUT_STATUS_PREBUILT;
        }
        if (compiler_driver_archive_input(input_path))
        {
            ObjectArchive* archive = &input_archives[input_index];
            compiler_driver_archive_extract(arena, &archive_state, archive, objects, &object_count);
            if (archive->error != OBJECT_ERROR_NONE)
            {
                compiler_driver_fail_input(&result, inputs, input_index, COMPILER_DRIVER_ERROR_OBJECT, archive->error,
                                              string_format(arena, S8("could not read archive {S8}: {S8}"), input_path, archive->diagnostic));
                goto finish;
            }
            continue;
        }
        if (compiler_driver_object_input(input_path))
        {
            FileMapRead object_file = file_map_read(arena, input_path, (FileReadOptions){0});
            if (!object_file.bytes.pointer)
            {
                compiler_driver_fail_input(&result, inputs, input_index, COMPILER_DRIVER_ERROR_FILE_READ, OBJECT_ERROR_NONE,
                                              string_format(arena, S8("could not read {S8}"), input_path));
                file_map_unmap(object_file);
                goto finish;
            }
            ObjectFile object = object_read(arena, object_file.bytes, invocation.target);
            if (object.requires_executable_stack) object.executable_stack_source = input_path;
            file_map_unmap(object_file);
            if (object.error != OBJECT_ERROR_NONE)
            {
                compiler_driver_fail_input(&result, inputs, input_index, COMPILER_DRIVER_ERROR_OBJECT, object.error,
                                              object.diagnostic.length
                                                  ? string_format(arena, S8("could not read object {S8}: {S8}"), input_path, object.diagnostic)
                                                  : string_format(arena, S8("could not read object {S8}: error {u32}"), input_path, (u32)object.error));
                goto finish;
            }
            objects[object_count++] = object;
            continue;
        }
        CompilerDriverInvocation single = invocation;
        single.input_paths = invocation.input_paths + input_index;
        single.input_languages = invocation.input_languages ? invocation.input_languages + input_index : 0;
        single.input_count = 1;
        single.input_language_count = single.input_languages ? 1 : 0;
        single.link_operations = 0;
        single.link_operation_count = 0;
        single.output_path = (String8){0};
        bool suppress_object_write = link_inputs_retained;
        if (!invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
        {
            single.output_path = compiler_driver_default_object_path(arena, invocation.input_paths[input_index]);
        }
        else if (link_inputs_retained)
        {
            single.action = COMPILER_DRIVER_ACTION_OBJECT;
        }
        bool unit_in_result_arena = invocation.input_count == 1;
        Arena* unit_arena;
        CompilerDriverResult unit;
        CompilerDriverUnit* task = 0;
        CompilerDriverUnitMetrics serial_metrics;
        CompilerDriverUnitMetrics* unit_metrics = 0;
        CompilerDriverArenaWatch watch = {0};
        u32 first_record = warnings.record_count;
        if (unit_in_result_arena)
        {
            unit_arena = arena;
            if (inputs && measure)
            {
                watch = compiler_driver_arena_watch_begin(arena);
                unit_metrics = &serial_metrics;
            }
            unit = compiler_driver_execute_unit(arena, single, suppress_object_write, &warnings, unit_metrics);
            if (unit_metrics)
            {
                compiler_driver_arena_watch_end(&watch, unit_metrics);
            }
        }
        else if (compiler_driver_parallel_c_input(invocation, input_index))
        {
            if (input_index >= batch_end)
            {
                if (!unit_tasks)
                {
                    unit_tasks = arena_allocate(arena, CompilerDriverUnit, unit_task_capacity);
                }
                batch_first = input_index;
                unit_task_count = 1;
                while (unit_task_count < unit_task_capacity && unit_task_count < invocation.input_count - input_index &&
                       unit_task_count < link_operation_count - operation_index)
                {
                    CompilerDriverLinkOperation next_operation = compiler_driver_link_operation(&invocation, operation_index + unit_task_count);
                    if (next_operation.kind != COMPILER_DRIVER_LINK_OPERATION_FILE || next_operation.index != input_index + unit_task_count ||
                        !compiler_driver_parallel_c_input(invocation, next_operation.index))
                    {
                        break;
                    }
                    unit_task_count += 1;
                }
                batch_end = batch_first + unit_task_count;
                memset(unit_tasks, 0, sizeof(*unit_tasks) * unit_task_count);
                // Arena pools are per thread (arena.c), and this thread destroys
                // every slot after ordered publication, so it creates them too.
                // A lane-created arena would park here where no worker can take
                // it back: every cohort would reserve and fault fresh worker
                // arenas while this pool filled toward ARENA_POOL_LIMIT
                // committed TU arenas across cohorts and invocations. Created
                // here, at most one arena per slot circulates. A failed
                // creation stays a null slot and is diagnosed in input order.
                for (u32 index = 0; index < unit_task_count; index += 1)
                {
                    unit_tasks[index].arena = arena_create((ArenaCreation){
                        .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
                        .flags = {.pool_reuse = 1},
                    });
                }
                if (unit_task_count > 1 && !units_prewarmed)
                {
                    compiler_parallel_prewarm();
                    units_prewarmed = true;
                }
                CompilerDriverUnitBatch batch = {
                    .invocation = invocation, .units = unit_tasks, .first_input = batch_first, .count = unit_task_count,
                };
                lane_run(unit_task_count, &compiler_driver_unit_lane, &batch);
                result.compilation_workers = BUSTER_MAX(result.compilation_workers, batch.workers);
            }
            task = &unit_tasks[input_index - batch_first];
            unit_arena = task->arena;
            unit = task->result;
            if (inputs && measure && unit_arena)
            {
                unit_metrics = &task->metrics;
            }
            // Only the ordered prefix is observable. Later completed inputs
            // are discarded if this input fails, including their warnings.
            for (u32 index = 0; index < task->warnings.record_count; index += 1)
            {
                compiler_driver_collect_diagnostic(&warnings, task->warnings.records[index]);
            }
            for (CompilerDriverWarningChunk* chunk = task->warnings.first; chunk; chunk = chunk->next)
            {
                compiler_driver_warning_append_text(&warnings, string_duplicate_arena(arena, chunk->text, false));
            }
        }
        else
        {
            unit_arena = arena_create((ArenaCreation){
                .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
                .flags = {.pool_reuse = 1},
            });
            if (unit_arena && inputs && measure)
            {
                watch = compiler_driver_arena_watch_begin(unit_arena);
                unit_metrics = &serial_metrics;
            }
            unit = unit_arena ? compiler_driver_execute_unit(unit_arena, single, suppress_object_write, &warnings, unit_metrics)
                              : (CompilerDriverResult){0};
            if (unit_metrics)
            {
                compiler_driver_arena_watch_end(&watch, unit_metrics);
            }
        }
        if (!unit_arena)
        {
            // Allocation failure is this input's failure: -fkeep-going
            // records it and moves on like any other.
            compiler_driver_fail_input(&result, inputs, input_index, COMPILER_DRIVER_ERROR_INVALID_INPUT, OBJECT_ERROR_NONE,
                                          S8("could not allocate C translation-unit arena"));
            if (!invocation.keep_going)
            {
                goto finish;
            }
            continue;
        }
        if (inputs)
        {
            compiler_driver_input_record(arena, &invocation, &inputs[input_index], &unit, &warnings, first_record, unit_metrics);
        }
        result.tokenizer_error_count += unit.tokenizer_error_count;
        result.tokenizer_warning_count += unit.tokenizer_warning_count;
        result.parser_diagnostic_count += unit.parser_diagnostic_count;
        result.analysis_diagnostic_count += unit.analysis_diagnostic_count;
        // Each unit dedups its own include closure, so across several inputs
        // the unique aggregate is a sum of per-unit uniques and still counts
        // a shared header once per unit that included it.
        c_source_metrics_add(&result.source_lexed, &unit.source_lexed);
        c_source_metrics_add(&result.source_unique, &unit.source_unique);
        // Only the amplification rows survive the unit arena: a row lexed
        // once is not amplification, and a header shared between units is
        // legitimately lexed once per unit, so per-unit rows are appended
        // rather than merged by path.
        for (u32 row_index = 0; row_index < unit.lexed_file_count; row_index += 1)
        {
            CSourceFileMetrics row = unit.lexed_files[row_index];
            if (row.lex_count > 1)
            {
                if (result.lexed_file_count == result.lexed_files_reserved)
                {
                    u32 row_capacity = result.lexed_files_reserved ? result.lexed_files_reserved * 2 : 64;
                    CSourceFileMetrics* rows = arena_allocate(arena, CSourceFileMetrics, row_capacity);
                    if (result.lexed_file_count)
                    {
                        memcpy(rows, result.lexed_files, result.lexed_file_count * sizeof(*rows));
                    }
                    result.lexed_files = rows;
                    result.lexed_files_reserved = row_capacity;
                }
                row.path = string_duplicate_arena(arena, row.path, false);
                result.lexed_files[result.lexed_file_count] = row;
                result.lexed_file_count += 1;
            }
        }
        result.preprocessed.tokens += unit.preprocessed.tokens;
        result.preprocessed.bytes += unit.preprocessed.bytes;
        result.preprocessed.spelling_bytes += unit.preprocessed.spelling_bytes;
        result.preprocessed.expansions += unit.preprocessed.expansions;
        result.preprocessed.definitions += unit.preprocessed.definitions;
        result.direct_ssa.functions += unit.direct_ssa.functions;
        result.direct_ssa.locals += unit.direct_ssa.locals;
        result.direct_ssa.fallback_locals += unit.direct_ssa.fallback_locals;
        result.direct_ssa.temporaries += unit.direct_ssa.temporaries;
        result.direct_ssa.reads += unit.direct_ssa.reads;
        result.direct_ssa.writes += unit.direct_ssa.writes;
        result.direct_ssa.parameters_created += unit.direct_ssa.parameters_created;
        result.direct_ssa.parameters_removed += unit.direct_ssa.parameters_removed;
        result.type_layout.solves += unit.type_layout.solves;
        result.type_layout.pass_solves += unit.type_layout.pass_solves;
        result.type_layout.pass_state_types += unit.type_layout.pass_state_types;
        result.type_layout.pass_attempts += unit.type_layout.pass_attempts;
        result.type_layout.agenda_solves += unit.type_layout.agenda_solves;
        result.type_layout.agenda_types += unit.type_layout.agenda_types;
        result.type_layout.agenda_attempts += unit.type_layout.agenda_attempts;
        result.type_layout.agenda_edges += unit.type_layout.agenda_edges;
        result.type_layout.agenda_notifications += unit.type_layout.agenda_notifications;
        result.type_layout.agenda_pushes += unit.type_layout.agenda_pushes;
        result.type_layout.agenda_fallbacks += unit.type_layout.agenda_fallbacks;
        result.local_promotion.candidate_locals += unit.local_promotion.candidate_locals;
        result.local_promotion.promoted_locals += unit.local_promotion.promoted_locals;
        result.local_promotion.removed_loads += unit.local_promotion.removed_loads;
        result.local_promotion.removed_stores += unit.local_promotion.removed_stores;
        result.local_promotion.inserted_parameters += unit.local_promotion.inserted_parameters;
        result.local_promotion.removed_parameters += unit.local_promotion.removed_parameters;
        result.local_promotion.uninitialized_locals += unit.local_promotion.uninitialized_locals;
        result.local_promotion.barrier_functions += unit.local_promotion.barrier_functions;
        result.local_promotion.instructions_before += unit.local_promotion.instructions_before;
        result.local_promotion.instructions_after += unit.local_promotion.instructions_after;
        result.local_promotion.values_before += unit.local_promotion.values_before;
        result.local_promotion.values_after += unit.local_promotion.values_after;
        result.local_promotion.parameter_sweeps += unit.local_promotion.parameter_sweeps;
        result.local_promotion.parameter_block_visits += unit.local_promotion.parameter_block_visits;
        result.local_promotion.parameter_visits += unit.local_promotion.parameter_visits;
        result.local_promotion.parameter_incoming_visits += unit.local_promotion.parameter_incoming_visits;
        for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1)
        {
            result.fast.passes[pass].nanoseconds += unit.fast.passes[pass].nanoseconds;
            result.fast.passes[pass].visits += unit.fast.passes[pass].visits;
            result.fast.passes[pass].changes += unit.fast.passes[pass].changes;
        }
        result.fast.functions += unit.fast.functions;
        result.fast.validation_skips += unit.fast.validation_skips;
        result.fast.budget_skips += unit.fast.budget_skips;
        result.fast.provenance_skips += unit.fast.provenance_skips;
        result.fast.parameter_budget_hits += unit.fast.parameter_budget_hits;
        result.fast.scratch_peak_bytes = BUSTER_MAX(result.fast.scratch_peak_bytes, unit.fast.scratch_peak_bytes);
        result.fast.retained_bytes += unit.fast.retained_bytes;
        result.fast.compact_nanoseconds += unit.fast.compact_nanoseconds;
        result.fast.instructions_before += unit.fast.instructions_before;
        result.fast.instructions_after += unit.fast.instructions_after;
        codegen_statistics_add(&result.codegen_statistics, &unit.codegen_statistics);
        object_write_statistics_add(&result.object_write_statistics, &unit.object_write_statistics);
        if (unit.fallback_record_count)
        {
            u64 needed = (u64)result.fallback_record_count + unit.fallback_record_count;
            if (needed > UINT32_MAX)
            {
                if (result.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    result.error = COMPILER_DRIVER_ERROR_CODEGEN;
                    result.codegen_error = CODEGEN_ERROR_CAPACITY;
                    result.diagnostic = S8("native fallback census exceeds its record limit");
                }
                if (task) { task->arena = 0; }
                if (!unit_in_result_arena)
                {
                    arena_destroy(unit_arena, 1);
                }
                goto finish;
            }
            if (needed > fallback_record_capacity)
            {
                u64 grown_capacity = BUSTER_MAX(needed, (u64)fallback_record_capacity * 2);
                fallback_record_capacity = (u32)BUSTER_MIN(grown_capacity, UINT32_MAX);
                CompilerDriverFallbackRecord* records = arena_allocate(arena, CompilerDriverFallbackRecord, fallback_record_capacity);
                if (result.fallback_record_count)
                {
                    memcpy(records, result.fallback_records, sizeof(*records) * result.fallback_record_count);
                }
                result.fallback_records = records;
            }
            for (u32 index = 0; index < unit.fallback_record_count; index += 1)
            {
                CompilerDriverFallbackRecord record = unit.fallback_records[index];
                record.source = string_duplicate_arena(arena, record.source, false);
                record.function = string_duplicate_arena(arena, record.function, false);
                result.fallback_records[result.fallback_record_count++] = record;
            }
        }
        if (unit.error != COMPILER_DRIVER_ERROR_NONE)
        {
            // The first failing input names the invocation's error. Under
            // -fkeep-going later inputs still compile, but nothing links.
            if (result.error == COMPILER_DRIVER_ERROR_NONE)
            {
                if (unit.diagnostic.length)
                {
                    result.diagnostic = string_duplicate_arena(arena, unit.diagnostic, false);
                }
                result.error = unit.error;
                result.codegen_error = unit.codegen_error;
                result.object_error = unit.object_error;
            }
            if (task)
            {
                task->arena = 0;
            }
            if (!unit_in_result_arena)
            {
                arena_destroy(unit_arena, 1);
            }
            if (!invocation.keep_going)
            {
                goto finish;
            }
            continue;
        }
        if (unit.has_object && link_inputs_retained)
        {
            if (unit_in_result_arena)
            {
                objects[object_count++] = unit.object;
            }
            else
            {
                ObjectFile object = {
                    .target = unit.object.target,
                    .error = unit.object.error,
                    .section_count = unit.object.section_count,
                    .symbol_count = unit.object.symbol_count,
                    .relocation_count = unit.object.relocation_count,
                    .debug_module_count = unit.object.debug_module_count,
                };
                object.sections = arena_allocate(arena, ObjectSection, object.section_count);
                for (u32 section_index = 0; section_index < object.section_count; section_index += 1)
                {
                    ObjectSection source = unit.object.sections[section_index];
                    ObjectSection* destination = &object.sections[section_index];
                    *destination = source;
                    destination->name = string_duplicate_arena(arena, source.name, false);
                    destination->data.pointer = arena_allocate(arena, u8, source.data.length);
                    if (source.data.length)
                    {
                        memcpy(destination->data.pointer, source.data.pointer, source.data.length);
                    }
                }
                object.symbols = arena_allocate(arena, ObjectSymbol, object.symbol_count);
                for (u32 symbol_index = 0; symbol_index < object.symbol_count; symbol_index += 1)
                {
                    object.symbols[symbol_index] = unit.object.symbols[symbol_index];
                    object.symbols[symbol_index].name = string_duplicate_arena(arena, unit.object.symbols[symbol_index].name, false);
                }
                object.relocations = arena_allocate(arena, ObjectRelocation, object.relocation_count);
                if (object.relocation_count)
                {
                    memcpy(object.relocations, unit.object.relocations, sizeof(ObjectRelocation) * object.relocation_count);
                }
                object.debug_modules = arena_allocate(arena, ObjectDebugModule, object.debug_module_count);
                for (u32 module_index = 0; module_index < object.debug_module_count; module_index += 1)
                {
                    object.debug_modules[module_index] = unit.object.debug_modules[module_index];
                    object.debug_modules[module_index].name = string_duplicate_arena(arena, unit.object.debug_modules[module_index].name, false);
                }
                // The initializer priorities are as much a part of the array
                // sections as their bytes are: the linker orders the whole
                // program's constructors by them, and a copy that dropped them
                // would leave a `constructor(101)` in this unit running after an
                // unprioritized one in another.
                for (u32 slot = 0; slot < 2; slot += 1)
                {
                    u32 kind = slot ? OBJECT_SECTION_FINI_ARRAY : OBJECT_SECTION_INIT_ARRAY;
                    u64 entries = unit.object.initializer_priorities[slot] && kind < unit.object.section_count
                                      ? unit.object.sections[kind].data.length / OBJECT_INITIALIZER_ENTRY_SIZE
                                      : 0;
                    if (entries)
                    {
                        object.initializer_priorities[slot] = arena_allocate(arena, u32, entries);
                        memcpy(object.initializer_priorities[slot], unit.object.initializer_priorities[slot], entries * sizeof(u32));
                    }
                }
                objects[object_count++] = object;
            }
        }
        if (unit.output.length)
        {
            preprocessed[input_index] = string_duplicate_arena(arena, unit.output, false);
        }
        if (task)
        {
            task->arena = 0;
        }
        if (!unit_in_result_arena)
        {
            arena_destroy(unit_arena, 1);
        }
    }
    if (result.error != COMPILER_DRIVER_ERROR_NONE)
    {
        goto finish;
    }
    if (archive_state.arena)
    {
        arena_destroy(archive_state.arena, 1);
        archive_state.arena = 0;
    }
    if (link_inputs_retained && compiler_driver_windows_runtime_object_target(invocation.target))
    {
        objects[object_count++] = link_windows_runtime_object(arena, invocation.target);
        // The UCRT exit-handler stubs are selected the way an archive member
        // is, unlike the `_fltused` marker above them: their undefined `_crt_`
        // reference would otherwise import into every image, and fail the link
        // where no ucrtbase.dll can be read.
        ObjectFile runtime = link_windows_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, objects, object_count))
        {
            objects[object_count++] = runtime;
        }
    }
    if (link_inputs_retained && compiler_driver_elf_runtime_object_target(invocation.target))
    {
        // Selected the way an archive member is: only a program that
        // references one of its stubs and defines none of them pulls it in.
        ObjectFile runtime = link_elf_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, objects, object_count))
        {
            objects[object_count++] = runtime;
        }
    }
    if (invocation.emit_llvm_bitcode)
    {
        goto finish;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.output = string_join_arena(arena,
                                          (SliceString8){
                                              .pointer = preprocessed,
                                              .length = invocation.input_count,
                                          },
                                          false);
        if (invocation.output_path.length)
        {
            compiler_driver_publish(arena, invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output), &result);
        }
        goto finish;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result.output = string_join_arena(arena,
                                          (SliceString8){
                                              .pointer = preprocessed,
                                              .length = invocation.input_count,
                                          },
                                          false);
        goto finish;
    }
    if (invocation.action != COMPILER_DRIVER_ACTION_LINK)
    {
        goto finish;
    }
    LinkObjectResult linked = link_objects(arena, objects, object_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                               .alias_single_input_sections = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.native_link.error = linked.error;
        result.native_link.symbol = linked.symbol;
        result.diagnostic = linked.symbol.length ? string_format(arena, S8("C object linking failed with {S8} on symbol '{S8}'"), link_error_name(linked.error), linked.symbol)
                                                 : string_format(arena, S8("C object linking failed with {S8}"), link_error_name(linked.error));
        goto finish;
    }
    result.object = linked.object;
    result.has_object = true;
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_executable_path(invocation.target);
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_target_dynamic_libraries(arena, invocation, static_libraries, &linked.object);
    if (dynamic_libraries.missing_request.length)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = dynamic_libraries.unsupported_script_path.length
                                ? string_format(arena, S8("unsupported GNU linker script {S8} requested by -l{S8}"),
                                                dynamic_libraries.unsupported_script_path, dynamic_libraries.missing_request)
                                : string_format(arena, S8("cannot find -l{S8}"), dynamic_libraries.missing_request);
        compiler_driver_dynamic_libraries_release(&dynamic_libraries);
        goto finish;
    }
    if (dynamic_libraries.missing_runtime_symbol.length)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.native_link.error = LINK_ERROR_UNRESOLVED_SYMBOL;
        result.native_link.symbol = dynamic_libraries.missing_runtime_symbol;
        result.diagnostic = string_format(arena, S8("target library {S8} does not provide callable compiler helper: {S8}"),
                                         dynamic_libraries.runtime_failure_library, dynamic_libraries.missing_runtime_symbol);
        compiler_driver_dynamic_libraries_release(&dynamic_libraries);
        goto finish;
    }
    result.native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = output,
                                                    .entry_symbol = invocation.entry_symbol.length ? invocation.entry_symbol
                                                                                                   : compiler_driver_default_entry_symbol(invocation.target),
                                                    .sysroot = invocation.sysroot,
                                                    .library_paths = invocation.library_paths,
                                                    .framework_paths = invocation.framework_paths,
                                                    .frameworks = invocation.frameworks,
                                                    .linker_arguments = invocation.linker_arguments,
                                                    .library_path_count = invocation.library_path_count,
                                                    .framework_path_count = invocation.framework_path_count,
                                                    .framework_count = invocation.framework_count,
                                                    .linker_argument_count = invocation.linker_argument_count,
                                                    .dynamic_libraries = dynamic_libraries.pointer,
                                                    .dynamic_library_count = dynamic_libraries.count,
                                                    .runtime_exported_symbols = dynamic_libraries.runtime.exported_symbols,
                                                    .runtime_data_symbols = dynamic_libraries.runtime.exported_data_symbols,
                                                    .runtime_versioned_symbols = dynamic_libraries.runtime.versioned_symbols,
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_data_symbol_count = dynamic_libraries.runtime.exported_data_symbol_count,
                                                    .runtime_versioned_symbol_count = dynamic_libraries.runtime.versioned_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                    .image_kind = (u8)invocation.image_kind,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    WORK_LEDGER_RECORD(OUTPUT_LINK_IMAGE_BYTES, result.native_link.executable.length);
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = compiler_driver_native_link_diagnostic(arena, invocation, result.native_link);
    }
finish:
    result.source_cache = c_source_cache_stats(invocation.source_cache);
    c_source_cache_destroy(owned_source_cache);
    if (archive_state.arena) arena_destroy(archive_state.arena, 1);
    for (u32 index = 0; input_archive_maps && index < invocation.input_count; index += 1) file_map_unmap(input_archive_maps[index]);
    for (u32 index = 0; library_archive_maps && index < invocation.library_count; index += 1) file_map_unmap(library_archive_maps[index]);
    // Every lane has joined before this unwind. A failed input can leave
    // later slots populated; none may outlive the driver invocation.
    for (u32 index = 0; index < unit_task_count; index += 1)
    {
        if (unit_tasks[index].arena)
        {
            arena_destroy(unit_tasks[index].arena, 1);
            unit_tasks[index].arena = 0;
        }
    }
    result.compilation_workers = BUSTER_MAX(result.compilation_workers, (u32)1);
    if (result.error != COMPILER_DRIVER_ERROR_NONE && !warnings.suppress_records)
    {
        bool has_error = false;
        for (u32 index = 0; index < warnings.record_count; index += 1)
        {
            has_error |= warnings.records[index].severity == COMPILER_DIAGNOSTIC_ERROR;
        }
        if (!has_error)
        {
            CompilerDiagnostic diagnostic = {
                .code = compiler_driver_error_code(result.error), .message = result.diagnostic,
                .primary = {.range = {.source = IR_SOURCE_ID_INVALID}},
            };
            if (result.error == COMPILER_DRIVER_ERROR_LINK && result.native_link.error != LINK_ERROR_NONE)
            {
                diagnostic.code = compiler_driver_link_code(result.native_link.error);
                diagnostic.symbol = result.native_link.symbol;
            }
            compiler_driver_collect_diagnostic(&warnings, diagnostic);
        }
    }
    result.diagnostics = warnings.records;
    result.diagnostic_count = warnings.record_count;
    result.warning = compiler_driver_warning_flatten(warnings);
    result.inputs = inputs;
    result.input_result_count = inputs ? invocation.input_count : 0;
    result.failed_input_count = 0;
    for (u32 index = 0; index < result.input_result_count; index += 1)
    {
        result.failed_input_count += (u32)(inputs[index].status == COMPILER_DRIVER_INPUT_STATUS_REJECTED ||
                                           inputs[index].status == COMPILER_DRIVER_INPUT_STATUS_FAILED);
    }
    return result;
}

// The -fmetrics-out record set follows the tagged `NAME version=N key=value`
// convention of the CODEGEN_FALLBACK_FUNCTION census lines: one record per
// line, fields in a fixed order, numbers in decimal, strings as lowercase
// hex (`-` when empty) so no value needs quoting or escaping. Readers key off
// the fields they know; a later version only appends fields.
typedef struct CompilerDriverMetricsText CompilerDriverMetricsText;
struct CompilerDriverMetricsText
{
    Arena* arena;
    String8* pieces;
    u64 count;
    u64 capacity;
};

BUSTER_GLOBAL_LOCAL void compiler_driver_metrics_piece(CompilerDriverMetricsText* text, String8 piece)
{
    if (text->count == text->capacity)
    {
        u64 capacity = text->capacity ? text->capacity * 2 : 256;
        String8* pieces = arena_allocate(text->arena, String8, capacity);
        if (text->count)
        {
            memcpy(pieces, text->pieces, sizeof(*pieces) * text->count);
        }
        text->pieces = pieces;
        text->capacity = capacity;
    }
    text->pieces[text->count] = piece;
    text->count += 1;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_metrics_u64(CompilerDriverMetricsText* text, String8 key, u64 value)
{
    compiler_driver_metrics_piece(text, string_format(text->arena, S8(" {S8}={u64}"), key, value));
}

BUSTER_GLOBAL_LOCAL void compiler_driver_metrics_word(CompilerDriverMetricsText* text, String8 key, String8 value)
{
    compiler_driver_metrics_piece(text, string_format(text->arena, S8(" {S8}={S8}"), key, value));
}

// Hex of at most `limit` bytes; the caller records the full length when a
// field can be truncated.
BUSTER_GLOBAL_LOCAL void compiler_driver_metrics_hex(CompilerDriverMetricsText* text, String8 key, String8 value, u64 limit)
{
    u64 length = BUSTER_MIN(value.length, limit);
    String8 encoded = S8("-");
    if (length)
    {
        char8 const digits[] = "0123456789abcdef";
        char8* hex = arena_allocate(text->arena, char8, length * 2);
        for (u64 index = 0; index < length; index += 1)
        {
            u8 byte = (u8)value.pointer[index];
            hex[index * 2] = digits[byte >> 4];
            hex[index * 2 + 1] = digits[byte & 15];
        }
        encoded = (String8){.pointer = hex, .length = length * 2};
    }
    compiler_driver_metrics_word(text, key, encoded);
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_input_status_name(CompilerDriverInputStatus status)
{
    static const String8 names[] = {
        [COMPILER_DRIVER_INPUT_STATUS_NOT_RUN] = S8_INITIALIZER("not_run"),
        [COMPILER_DRIVER_INPUT_STATUS_OK] = S8_INITIALIZER("ok"),
        [COMPILER_DRIVER_INPUT_STATUS_REJECTED] = S8_INITIALIZER("rejected"),
        [COMPILER_DRIVER_INPUT_STATUS_FAILED] = S8_INITIALIZER("failed"),
        [COMPILER_DRIVER_INPUT_STATUS_PREBUILT] = S8_INITIALIZER("prebuilt"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == COMPILER_DRIVER_INPUT_STATUS_COUNT);
    return (u32)status < (u32)BUSTER_ARRAY_LENGTH(names) ? names[status] : S8("unknown");
}

String8 compiler_driver_metrics_format(Arena* arena, CompilerDriverInvocation const* invocation, CompilerDriverResult const* result,
                                       CompilerDriverProcessMetrics process)
{
    static const String8 phase_keys[] = {
        [COMPILER_DRIVER_PHASE_READ] = S8_INITIALIZER("read_ns"),
        [COMPILER_DRIVER_PHASE_PREPROCESS] = S8_INITIALIZER("preprocess_ns"),
        [COMPILER_DRIVER_PHASE_PARSE] = S8_INITIALIZER("parse_ns"),
        [COMPILER_DRIVER_PHASE_ANALYSIS] = S8_INITIALIZER("analysis_ns"),
        [COMPILER_DRIVER_PHASE_IR] = S8_INITIALIZER("ir_ns"),
        [COMPILER_DRIVER_PHASE_CODEGEN] = S8_INITIALIZER("codegen_ns"),
        [COMPILER_DRIVER_PHASE_OBJECT] = S8_INITIALIZER("object_ns"),
        [COMPILER_DRIVER_PHASE_EMIT] = S8_INITIALIZER("emit_ns"),
    };
    static const String8 section_keys[] = {
        [COMPILER_DRIVER_SECTION_CLASS_TEXT] = S8_INITIALIZER("text_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_READ_ONLY_DATA] = S8_INITIALIZER("rodata_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_DATA] = S8_INITIALIZER("data_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_ZERO] = S8_INITIALIZER("bss_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_THREAD_LOCAL_DATA] = S8_INITIALIZER("tdata_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_THREAD_LOCAL_ZERO] = S8_INITIALIZER("tbss_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_INITIALIZER] = S8_INITIALIZER("initializer_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_UNWIND] = S8_INITIALIZER("unwind_bytes"),
        [COMPILER_DRIVER_SECTION_CLASS_DEBUG] = S8_INITIALIZER("debug_bytes"),
    };
    static const String8 action_names[] = {
        [COMPILER_DRIVER_ACTION_LINK] = S8_INITIALIZER("link"),
        [COMPILER_DRIVER_ACTION_PREPROCESS] = S8_INITIALIZER("preprocess"),
        [COMPILER_DRIVER_ACTION_ASSEMBLY] = S8_INITIALIZER("assembly"),
        [COMPILER_DRIVER_ACTION_OBJECT] = S8_INITIALIZER("object"),
        [COMPILER_DRIVER_ACTION_SYNTAX_ONLY] = S8_INITIALIZER("syntax-only"),
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(phase_keys) == COMPILER_DRIVER_PHASE_COUNT);
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(section_keys) == COMPILER_DRIVER_SECTION_CLASS_COUNT);
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(action_names) == COMPILER_DRIVER_ACTION_COUNT);
    CompilerDriverMetricsText text = {.arena = arena};
    u32 status_counts[COMPILER_DRIVER_INPUT_STATUS_COUNT] = {0};
    for (u32 index = 0; index < result->input_result_count; index += 1)
    {
        CompilerDriverInputStatus status = result->inputs[index].status;
        status_counts[(u32)status < COMPILER_DRIVER_INPUT_STATUS_COUNT ? status : COMPILER_DRIVER_INPUT_STATUS_NOT_RUN] += 1;
    }
    // A driver-level failure before any input record keeps every input
    // not_run; the header still reports the invocation's own error.
    compiler_driver_metrics_piece(&text, string_format(arena, S8("CC_METRICS version={u32} schema=buster-cc-metrics"), COMPILER_DRIVER_METRICS_VERSION));
    compiler_driver_metrics_u64(&text, S8("inputs"), invocation->input_count);
    compiler_driver_metrics_u64(&text, S8("records"), result->input_result_count);
    compiler_driver_metrics_u64(&text, S8("ok"), status_counts[COMPILER_DRIVER_INPUT_STATUS_OK]);
    compiler_driver_metrics_u64(&text, S8("rejected"), status_counts[COMPILER_DRIVER_INPUT_STATUS_REJECTED]);
    compiler_driver_metrics_u64(&text, S8("failed"), status_counts[COMPILER_DRIVER_INPUT_STATUS_FAILED]);
    compiler_driver_metrics_u64(&text, S8("not_run"), status_counts[COMPILER_DRIVER_INPUT_STATUS_NOT_RUN]);
    compiler_driver_metrics_u64(&text, S8("prebuilt"), status_counts[COMPILER_DRIVER_INPUT_STATUS_PREBUILT]);
    compiler_driver_metrics_word(&text, S8("error"), compiler_driver_error_code(result->error));
    compiler_driver_metrics_u64(&text, S8("exit_status"), process.exit_status);
    compiler_driver_metrics_word(&text, S8("action"), (u32)invocation->action < COMPILER_DRIVER_ACTION_COUNT ? action_names[invocation->action] : S8("unknown"));
    compiler_driver_metrics_word(&text, S8("target"), string_format(arena, S8("{S8}-{S8}"), cpu_arch_to_string_os(invocation->target.cpu_arch),
                                                                     operating_system_to_string_os(invocation->target.os)));
    compiler_driver_metrics_word(&text, S8("allocator"), codegen_register_allocator_mode_string((CodegenRegisterAllocatorMode)invocation->register_allocator));
    compiler_driver_metrics_u64(&text, S8("compile_jobs"), invocation->compile_jobs ? invocation->compile_jobs : 1);
    compiler_driver_metrics_u64(&text, S8("compilation_workers"), result->compilation_workers);
    // One worker runs the units one after another; lanes (link mode only)
    // run several at once, so their intervals may overlap.
    compiler_driver_metrics_word(&text, S8("intervals"), result->compilation_workers > 1 ? S8("concurrent") : S8("serial"));
    compiler_driver_metrics_u64(&text, S8("keep_going"), invocation->keep_going);
    compiler_driver_metrics_u64(&text, S8("function_sizes"), invocation->collect_function_sizes);
    compiler_driver_metrics_u64(&text, S8("wall_ns"), process.wall_nanoseconds);
    compiler_driver_metrics_u64(&text, S8("peak_rss_bytes"), process.peak_resident_bytes);
    compiler_driver_metrics_piece(&text, S8("\n"));
    for (u32 index = 0; index < result->input_result_count; index += 1)
    {
        CompilerDriverInputResult const* input = &result->inputs[index];
        compiler_driver_metrics_piece(&text, string_format(arena, S8("CC_METRICS_INPUT version={u32}"), COMPILER_DRIVER_METRICS_VERSION));
        compiler_driver_metrics_u64(&text, S8("index"), input->index);
        compiler_driver_metrics_word(&text, S8("status"), compiler_driver_input_status_name(input->status));
        compiler_driver_metrics_word(&text, S8("error"), compiler_driver_error_code(input->error));
        compiler_driver_metrics_u64(&text, S8("errors"), input->error_count);
        compiler_driver_metrics_u64(&text, S8("warnings"), input->warning_count);
        compiler_driver_metrics_u64(&text, S8("measured"), input->measured);
        compiler_driver_metrics_u64(&text, S8("start_ns"), input->start_nanoseconds);
        compiler_driver_metrics_u64(&text, S8("end_ns"), input->end_nanoseconds);
        compiler_driver_metrics_u64(&text, S8("total_ns"), input->end_nanoseconds - input->start_nanoseconds);
        for (u32 phase = 0; phase < COMPILER_DRIVER_PHASE_COUNT; phase += 1)
        {
            compiler_driver_metrics_u64(&text, phase_keys[phase], input->phase_nanoseconds[phase]);
        }
        compiler_driver_metrics_u64(&text, S8("arena_peak_bytes"), input->arena_peak_bytes);
        compiler_driver_metrics_u64(&text, S8("arena_retained_bytes"), input->arena_retained_bytes);
        compiler_driver_metrics_u64(&text, S8("source_bytes"), input->source_bytes);
        compiler_driver_metrics_u64(&text, S8("preprocessed_tokens"), input->preprocessed_tokens);
        compiler_driver_metrics_u64(&text, S8("object_file_bytes"), input->object_file_bytes);
        for (u32 section = 0; section < COMPILER_DRIVER_SECTION_CLASS_COUNT; section += 1)
        {
            compiler_driver_metrics_u64(&text, section_keys[section], input->section_bytes[section]);
        }
        compiler_driver_metrics_u64(&text, S8("codegen_functions"), input->codegen.function_count);
        compiler_driver_metrics_u64(&text, S8("instructions"), input->codegen.instruction_count);
        compiler_driver_metrics_u64(&text, S8("values"), input->codegen.value_count);
        compiler_driver_metrics_u64(&text, S8("code_bytes"), input->codegen.code_bytes);
        compiler_driver_metrics_u64(&text, S8("stack_frame_bytes"), input->codegen.stack_frame_bytes);
        compiler_driver_metrics_u64(&text, S8("max_stack_frame_bytes"), input->codegen.maximum_stack_frame_bytes);
        compiler_driver_metrics_u64(&text, S8("fallback_functions"), input->codegen.fallback_function_count);
        compiler_driver_metrics_u64(&text, S8("fallback_records"), input->fallback_record_count);
        compiler_driver_metrics_u64(&text, S8("function_records"), input->function_count);
        compiler_driver_metrics_u64(&text, S8("function_records_omitted"), input->functions_omitted);
        compiler_driver_metrics_u64(&text, S8("diagnostic_records"), input->diagnostic_record_count);
        compiler_driver_metrics_word(&text, S8("diagnostic_digest"), input->diagnostic_digest.length ? input->diagnostic_digest : S8("-"));
        compiler_driver_metrics_u64(&text, S8("diagnostic_line"), input->diagnostic_line);
        compiler_driver_metrics_u64(&text, S8("diagnostic_column"), input->diagnostic_column);
        compiler_driver_metrics_hex(&text, S8("path_hex"), input->path, input->path.length);
        compiler_driver_metrics_hex(&text, S8("diagnostic_code_hex"), input->diagnostic_code, input->diagnostic_code.length);
        compiler_driver_metrics_hex(&text, S8("diagnostic_path_hex"), input->diagnostic_path, input->diagnostic_path.length);
        compiler_driver_metrics_u64(&text, S8("message_bytes"), input->message.length);
        compiler_driver_metrics_u64(&text, S8("message_truncated"), input->message.length > COMPILER_DRIVER_METRICS_TEXT_LIMIT);
        compiler_driver_metrics_hex(&text, S8("message_hex"), input->message, COMPILER_DRIVER_METRICS_TEXT_LIMIT);
        compiler_driver_metrics_piece(&text, S8("\n"));
        for (u32 function = 0; function < input->function_count; function += 1)
        {
            CompilerDriverFunctionSize size = input->functions[function];
            compiler_driver_metrics_piece(&text, string_format(arena, S8("CC_METRICS_FUNCTION version={u32}"), COMPILER_DRIVER_METRICS_VERSION));
            compiler_driver_metrics_u64(&text, S8("input"), input->index);
            compiler_driver_metrics_u64(&text, S8("ordinal"), function);
            compiler_driver_metrics_u64(&text, S8("code_bytes"), size.code_bytes);
            compiler_driver_metrics_u64(&text, S8("name_bytes"), size.name.length);
            compiler_driver_metrics_u64(&text, S8("name_truncated"), size.name.length > COMPILER_DRIVER_METRICS_TEXT_LIMIT);
            compiler_driver_metrics_hex(&text, S8("name_hex"), size.name, COMPILER_DRIVER_METRICS_TEXT_LIMIT);
            compiler_driver_metrics_piece(&text, S8("\n"));
        }
    }
    return string_join_arena(arena, (SliceString8){.pointer = text.pieces, .length = text.count}, false);
}
