// Source-equivalence testing, not random text mutation. meta_source renders a
// bounded unsigned-integer grammar, aggregate/call relations and host oracle.
// meta_pair isolates compiler/guest crashes with deadlines; meta_reduce only
// edits grammar parameters and retains the failure signature. meta_run owns
// the capability matrix, deduplication and persistent failure bundles.
// Native smoke coverage is part of ide test. ide metamorphic runs the wider
// native/LLVM/Wasm64/eBPF campaign and reports unexecuted targets explicitly.
#include <buster/tests/compiler/metamorphic/metamorphic_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/target.h>
#include <buster/lib/time.h>
#include <buster/tests/compiler/codegen/ebpf_test_vm.h>

#define BUSTER_META_TRANSFORM_COUNT 11u
#define BUSTER_META_ALL_TRANSFORMS ((1u << BUSTER_META_TRANSFORM_COUNT) - 1u)
#define BUSTER_META_MAX_TERMS 3u
#define BUSTER_META_MAX_CASES 256u
#define BUSTER_META_MAX_REDUCTIONS 64u
#define BUSTER_META_MAX_SIGNATURES 16u
#define BUSTER_META_TIMEOUT_US UINT64_C(10000000)
#define BUSTER_META_RENAME 1u
#define BUSTER_META_DECLARATIONS 2u
#define BUSTER_META_PARENTHESES 4u
#define BUSTER_META_TEMPORARIES 8u
#define BUSTER_META_CONTROL_FLOW 16u
#define BUSTER_META_DEAD_CODE 32u
#define BUSTER_META_COMMUTE 64u
#define BUSTER_META_LAYOUT 128u
#define BUSTER_META_EMPTY_PASTE 256u
#define BUSTER_META_AGGREGATES 512u
#define BUSTER_META_OUTLINE 1024u
#define BUSTER_META_REFERENCE_COUNT 4u

typedef struct MetaSpec MetaSpec;
struct MetaSpec
{
    u32 seed;
    u32 terms;
    u32 rounds;
    u32 salt;
    u32 factor;
    u32 input_count;
};

typedef struct MetaText MetaText;
struct MetaText
{
    String8 parts[256];
    u32 count;
};

typedef enum MetaBackend
{
    META_NATIVE,
    META_LLVM,
    META_WASM,
    META_EBPF,
} MetaBackend;

typedef struct MetaTarget MetaTarget;
struct MetaTarget
{
    String8 name;
    String8 triple;
    CpuArch arch;
    OperatingSystem os;
    MetaBackend backend;
};

BUSTER_GLOBAL_LOCAL MetaTarget const meta_targets[] = {
    {S8_INITIALIZER("linux-x64"), S8_INITIALIZER("x86_64-unknown-linux-gnu"), CPU_ARCH_X86_64, OPERATING_SYSTEM_LINUX, META_NATIVE},
    {S8_INITIALIZER("linux-arm64"), S8_INITIALIZER("aarch64-unknown-linux-gnu"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_LINUX, META_NATIVE},
    {S8_INITIALIZER("windows-x64"), S8_INITIALIZER("x86_64-pc-windows-msvc"), CPU_ARCH_X86_64, OPERATING_SYSTEM_WINDOWS, META_NATIVE},
    {S8_INITIALIZER("windows-arm64"), S8_INITIALIZER("aarch64-pc-windows-msvc"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_WINDOWS, META_NATIVE},
    {S8_INITIALIZER("macos-x64"), S8_INITIALIZER("x86_64-apple-macos"), CPU_ARCH_X86_64, OPERATING_SYSTEM_MACOS, META_NATIVE},
    {S8_INITIALIZER("macos-arm64"), S8_INITIALIZER("aarch64-apple-macos"), CPU_ARCH_AARCH64, OPERATING_SYSTEM_MACOS, META_NATIVE},
    {S8_INITIALIZER("llvm-native"), {0}, CPU_ARCH_X86_64, OPERATING_SYSTEM_FREESTANDING, META_LLVM},
    {S8_INITIALIZER("wasm64"), S8_INITIALIZER("wasm64-unknown-freestanding"), CPU_ARCH_WASM64, OPERATING_SYSTEM_FREESTANDING, META_WASM},
    {S8_INITIALIZER("ebpf"), S8_INITIALIZER("bpfel-unknown-linux"), CPU_ARCH_BPFEL, OPERATING_SYSTEM_LINUX, META_EBPF},
};

BUSTER_GLOBAL_LOCAL u64 const meta_inputs[][2] = {
    {0, 0}, {1, 2}, {17, 41}, {UINT32_MAX, 7},
    {UINT64_C(1) << 32, 31}, {UINT64_C(1) << 63, 0},
    {UINT64_MAX, 1}, {UINT64_MAX, UINT64_MAX},
};

typedef struct MetaContext MetaContext;
struct MetaContext
{
    Arena* arena;
    String8 directory;
    AtomicU64 work_serial;
    String8 compiler;
    String8 clang;
    String8 gcc;
    String8 reference_compiler;
    bool reference_optimized;
    String8 node;
    String8 qemu;
    String8 wine;
    bool full;
    bool frontend_ssa;
    bool node_memory64_flag;
    String8 target_filter;
};

typedef enum MetaPhase
{
    META_PHASE_WRITE,
    META_PHASE_COMPILE,
    META_PHASE_CONSUME,
    META_PHASE_EXECUTE,
    META_PHASE_UNEXECUTED,
    META_PHASE_RUNNER,
} MetaPhase;

typedef struct MetaOutcome MetaOutcome;
struct MetaOutcome
{
    MetaPhase phase;
    ProcessWaitResult wait;
    bool launched;
    String8 command;
    String8 diagnostic;
};

typedef struct MetaPair MetaPair;
struct MetaPair
{
    MetaOutcome base;
    MetaOutcome changed;
    String8 directory;
    bool passed;
    bool executed;
};

typedef struct MetaSummary MetaSummary;
struct MetaSummary
{
    u32 pairs;
    u32 passed;
    u32 executed;
    u32 unexecuted;
    u32 failures;
    u32 reductions;
    u32 unique_failures;
    u32 reference_pairs;
};

BUSTER_GLOBAL_LOCAL void meta_append(MetaText* text, String8 part)
{
    BUSTER_CHECK(text->count < BUSTER_ARRAY_LENGTH(text->parts));
    text->parts[text->count++] = part;
}

BUSTER_GLOBAL_LOCAL String8 meta_finish(Arena* arena, MetaText* text)
{
    return string_join_arena(arena, (SliceString8){text->parts, text->count}, true);
}

BUSTER_GLOBAL_LOCAL u64 meta_expected(MetaSpec spec, u64 a, u64 b)
{
    u64 result = a + b;
    for (u32 term = 0; term < spec.terms; term += 1)
    {
        result += (a + spec.salt + term) * (b + spec.factor);
    }
    for (u32 round = 0; round < spec.rounds; round += 1)
    {
        result += a == b ? spec.salt : spec.factor;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL MetaSpec meta_spec(u32 seed)
{
    u32 bits = seed * UINT32_C(1664525) + UINT32_C(1013904223);
    MetaSpec result = {seed, 1u + bits % BUSTER_META_MAX_TERMS, 1u + (bits >> 8) % 3u,
                       (bits >> 16) % 17u, 1u + (bits >> 24) % 13u, BUSTER_ARRAY_LENGTH(meta_inputs)};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 meta_name(Arena* arena, String8 plain, u32 mask)
{
    String8 result = mask & BUSTER_META_RENAME ? string_format(arena, S8("renamed_{S8}"), plain) : plain;
    if (mask & BUSTER_META_EMPTY_PASTE)
    {
        result = string_format(arena, S8("META_NAME(,{S8})"), result);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 meta_operand(Arena* arena, String8 name, u32 mask)
{
    String8 result = mask & BUSTER_META_PARENTHESES ? string_format(arena, S8("((({S8})))"), name) : name;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 meta_layout(Arena* arena, String8 source)
{
    // The generated grammar has no quoted literals. Preserve preprocessor
    // lines byte-for-byte; change whitespace only at known token boundaries.
    char8* data = arena_allocate(arena, char8, source.length * 16 + 1);
    u64 length = 0;
    bool directive = false;
    bool line_start = true;
    for (u64 index = 0; index < source.length; index += 1)
    {
        char8 value = source.pointer[index];
        if (line_start && value == '#')
        {
            directive = true;
        }
        if (!directive && value == ' ')
        {
            String8 space = S8("\t/* meta */ ");
            memcpy(data + length, space.pointer, (size_t)space.length);
            length += space.length;
        }
        else data[length++] = value;
        line_start = value == '\n';
        if (line_start)
        {
            directive = false;
        }
    }
    data[length] = 0;
    return (String8){data, length};
}

BUSTER_GLOBAL_LOCAL u32 meta_target_transform_mask(MetaTarget target, u32 mask)
{
    // The bounded eBPF test interpreter has no local-call execution contract.
    // Retain every other requested relation; report this exclusion in meta_run.
    return target.backend == META_EBPF ? mask & ~BUSTER_META_OUTLINE : mask;
}

BUSTER_GLOBAL_LOCAL String8 meta_source(Arena* arena, MetaSpec spec, u32 mask, bool single_function)
{
    MetaText text = {0};
    if (mask & BUSTER_META_EMPTY_PASTE)
    {
        meta_append(&text, S8("#define META_NAME(prefix,name) prefix ## name\n"));
    }
    // Match the u64 evaluator by value range, not by an assumed sizeof/ABI.
    meta_append(&text, S8("typedef char meta_u64_width[(~0ULL == 18446744073709551615ULL) ? 1 : -1];\n"));
    String8 product = meta_name(arena, S8("product"), mask);
    if (mask & BUSTER_META_OUTLINE)
    {
        meta_append(&text, string_format(arena,
            S8("unsigned long long {S8}(unsigned long long left, unsigned long long right) {{\nreturn left * right;\n}}\n"), product));
    }
    meta_append(&text, S8("unsigned long long metamorphic(unsigned long long x, unsigned long long y) {\n"));
    String8 a = meta_name(arena, S8("a"), mask);
    String8 b = meta_name(arena, S8("b"), mask);
    String8 value = meta_name(arena, S8("value"), mask);
    String8 declaration_a = string_format(arena, S8("unsigned long long {S8} = x;\n"), a);
    String8 declaration_b = string_format(arena, S8("unsigned long long {S8} = y;\n"), b);
    meta_append(&text, mask & BUSTER_META_DECLARATIONS ? declaration_b : declaration_a);
    meta_append(&text, mask & BUSTER_META_DECLARATIONS ? declaration_a : declaration_b);
    if (mask & BUSTER_META_AGGREGATES)
    {
        // Explicit nested initialization defines both elements. Observe members,
        // never padding, and keep all reads within the two-element array.
        String8 aggregate = meta_name(arena, S8("aggregate"), mask);
        String8 copy = meta_name(arena, S8("aggregate_copy"), mask);
        meta_append(&text, S8("struct MetaValues { unsigned long long lane[2]; };\n"));
        meta_append(&text, string_format(arena, S8("struct MetaValues {S8} = {{{{ {S8}, {S8} }}}};\n"), aggregate, a, b));
        meta_append(&text, string_format(arena, S8("struct MetaValues {S8} = {S8};\n"), copy, aggregate));
        a = string_format(arena, S8("{S8}.lane[0]"), copy);
        b = string_format(arena, S8("{S8}.lane[1]"), copy);
    }
    a = meta_operand(arena, a, mask);
    b = meta_operand(arena, b, mask);
    meta_append(&text, string_format(arena, S8("unsigned long long {S8} = {S8} + {S8};\n"), value, a, b));
    for (u32 term = 0; term < spec.terms; term += 1)
    {
        String8 left = mask & BUSTER_META_COMMUTE ? string_format(arena, S8("({u32}ULL + {S8})"), spec.salt + term, a)
                                          : string_format(arena, S8("({S8} + {u32}ULL)"), a, spec.salt + term);
        String8 right = string_format(arena, S8("({S8} + {u32}ULL)"), b, spec.factor);
        if (mask & BUSTER_META_TEMPORARIES)
        {
            meta_append(&text, string_format(arena, S8("unsigned long long t{u32}a = {S8};\nunsigned long long t{u32}b = {S8};\n"),
                                             term, left, term, right));
            left = string_format(arena, S8("t{u32}a"), term);
            right = string_format(arena, S8("t{u32}b"), term);
        }
        if (mask & BUSTER_META_COMMUTE) { String8 swap = left; left = right; right = swap; }
        // Call operands are pure unsigned expressions: their unspecified
        // evaluation order cannot change the result or any observable state.
        String8 term_value = mask & BUSTER_META_OUTLINE ? string_format(arena, S8("{S8}({S8}, {S8})"), product, left, right)
                                                       : string_format(arena, S8("({S8} * {S8})"), left, right);
        meta_append(&text, string_format(arena, S8("{S8} = {S8} + {S8};\n"), value, value, term_value));
    }
    if (spec.rounds)
    {
        if (mask & BUSTER_META_CONTROL_FLOW)
        {
            meta_append(&text, string_format(arena, S8("unsigned int i = 0;\nwhile (i != {u32}U) {{\n"), spec.rounds));
            meta_append(&text, string_format(arena, S8("if ({S8} != {S8}) {S8} = {S8} + {u32}ULL; else {S8} = {S8} + {u32}ULL;\n"),
                                             a, b, value, value, spec.factor, value, value, spec.salt));
            meta_append(&text, S8("i = i + 1U;\n}\n"));
        }
        else
        {
            meta_append(&text, string_format(arena, S8("for (unsigned int i = 0; i < {u32}U; i = i + 1U) {{\n"), spec.rounds));
            meta_append(&text, string_format(arena, S8("if ({S8} == {S8}) {S8} = {S8} + {u32}ULL; else {S8} = {S8} + {u32}ULL;\n}}\n"),
                                             a, b, value, value, spec.salt, value, value, spec.factor));
        }
    }
    if (mask & BUSTER_META_DEAD_CODE)
    {
        meta_append(&text, string_format(arena, S8("if (0) {{ {S8} = {S8} + 42ULL; }}\n"), value, value));
    }
    meta_append(&text, string_format(arena, S8("return {S8};\n}}\n"), meta_operand(arena, value, mask)));
    if (!single_function)
    {
        meta_append(&text, S8("int main(void) {\nint status = 0;\n"));
        for (u32 input = 0; input < spec.input_count; input += 1)
        {
            u64 x = meta_inputs[input][0], y = meta_inputs[input][1];
            meta_append(&text, string_format(arena,
                S8("if (status == 0 && metamorphic({u64}ULL, {u64}ULL) != {u64}ULL) status = {u32};\n"),
                x, y, meta_expected(spec, x, y), input + 1));
        }
        meta_append(&text, S8("return status;\n}\n"));
    }
    String8 result = meta_finish(arena, &text);
    if (mask & BUSTER_META_LAYOUT)
    {
        result = meta_layout(arena, result);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 meta_diagnostic(String8 output)
{
    // Keep the diagnostic itself, not the source filename/line/column that
    // reduction intentionally changes. Non-source diagnostics stay intact.
    String8 result = output;
    String8 prefix = S8("cc: error: ");
    if (string_starts_with_sequence(result, prefix))
    {
        result = string_slice(result, prefix.length, result.length);
        u64 separator = string_first_sequence(result, S8(": "));
        if (separator != BUSTER_STRING_NO_MATCH)
        {
            result = string_slice(result, separator + 2, result.length);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL MetaOutcome meta_process(Arena* arena, SliceString8 argv, MetaPhase phase)
{
    MetaOutcome result = {.phase = phase, .wait = {.result = PROCESS_RESULT_NOT_EXISTENT}};
    MetaText command = {0};
    for (u64 index = 0; index < argv.length; index += 1)
    {
        meta_append(&command, string_format(arena, S8("argv[{u64}]={S8}\n"), index, argv.pointer[index]));
    }
    result.command = meta_finish(arena, &command);
    ProcessSpawnResult spawn = os_process_spawn(argv, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR), .use_process_environment = 1});
    result.launched = spawn.handle != 0;
    if (result.launched)
    {
        result.wait = os_process_wait_deadline(arena, spawn, BUSTER_META_TIMEOUT_US);
        ByteSlice diagnostic = result.wait.streams[STANDARD_STREAM_OUTPUT];
        if (!diagnostic.length)
        {
            diagnostic = result.wait.streams[STANDARD_STREAM_ERROR];
        }
        result.diagnostic = meta_diagnostic((String8){(char8*)diagnostic.pointer, diagnostic.length});
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool meta_success(MetaOutcome outcome)
{
    return outcome.launched && !outcome.wait.timed_out && outcome.wait.result == PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL bool meta_native_target(MetaTarget target)
{
    return target.backend == META_NATIVE && target.arch == target_native.cpu_arch && target.os == target_native.os;
}

BUSTER_GLOBAL_LOCAL bool meta_output_present(String8 path, bool require_content)
{
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = true}, (OpenPermissions){0});
    bool result = file != 0;
    if (file)
    {
        result = !require_content || os_file_get_size(file) != 0;
        os_file_close(file);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool meta_clear_output(String8 path)
{
    os_file_delete(path);
    return !meta_output_present(path, false);
}

BUSTER_GLOBAL_LOCAL MetaOutcome meta_require_output(MetaOutcome outcome, String8 path)
{
    if (meta_success(outcome) && !meta_output_present(path, true))
    {
        outcome.wait.result = PROCESS_RESULT_FAILED;
        outcome.diagnostic = S8("compiler reported success without a nonempty requested output\n");
        outcome.wait.streams[STANDARD_STREAM_ERROR] = BUSTER_SLICE_TO_BYTE_SLICE(outcome.diagnostic);
    }
    return outcome;
}

BUSTER_GLOBAL_LOCAL MetaOutcome meta_execute(MetaContext* context, String8 directory, MetaSpec spec, MetaTarget target,
                                               u32 allocator, String8 side, u32 mask, bool reference)
{
    Arena* arena = context->arena;
    String8 source = string_format_z(arena, S8("{S8}/{S8}.c"), directory, side);
    String8 artifact = string_format_z(arena, S8("{S8}/{S8}.artifact"), directory, side);
    String8 executable = string_format_z(arena, S8("{S8}/{S8}.exe"), directory, side);
    MetaOutcome result = {.phase = META_PHASE_WRITE, .wait = {.result = PROCESS_RESULT_FAILED}};
    bool single = target.backend == META_EBPF;
    String8 text = meta_source(arena, spec, mask, single);
    if (directory.length && context->compiler.length && meta_clear_output(executable) && meta_clear_output(artifact) &&
        file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(text)))
    {
        String8 argv[16];
        u32 count = 0;
        argv[count++] = reference ? context->reference_compiler : context->compiler;
        if (!reference)
        {
            argv[count++] = S8("cc");
        }
        argv[count++] = S8("-std=c11");
        argv[count++] = reference && context->reference_optimized ? S8("-O2") : S8("-O0");
        argv[count++] = S8("-nostdinc");
        argv[count++] = S8("-fwrapv");
        argv[count++] = S8("-fno-strict-aliasing");
        argv[count++] = S8("-funsigned-char");
        if (!reference)
        {
            argv[count++] = context->frontend_ssa ? S8("-ffrontend-ssa") : S8("-fno-frontend-ssa");
            argv[count++] = string_format(arena, S8("-fregister-allocator={S8}"),
                                          codegen_register_allocator_mode_string((CodegenRegisterAllocatorMode)allocator));
            if (target.triple.length) { argv[count++] = S8("-target"); argv[count++] = target.triple; }
            if (target.backend == META_LLVM)
            {
                argv[count++] = S8("-emit-llvm");
            }
        }
        argv[count++] = source;
        argv[count++] = S8("-o");
        argv[count++] = target.backend == META_NATIVE || reference ? executable : artifact;
        result = meta_process(arena, (SliceString8){argv, count}, META_PHASE_COMPILE);
        result = meta_require_output(result, argv[count - 1]);
        String8 compile_command = result.command;
        if (meta_success(result))
        {
            if (target.backend == META_LLVM && !reference && context->clang.length)
            {
                String8 consume[] = {context->clang, S8("-Wno-override-module"), S8("-x"), S8("ir"), artifact, S8("-o"), executable};
                result = meta_process(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(consume), META_PHASE_CONSUME);
                result = meta_require_output(result, executable);
                compile_command = string_format(arena, S8("{S8}\n{S8}"), compile_command, result.command);
            }
            if (meta_success(result))
            {
                String8 run[6];
                u32 run_count = 0;
                if (reference || meta_native_target(target) || (target.backend == META_LLVM && context->clang.length))
                    run[run_count++] = executable;
                else if (target.backend == META_WASM && context->node.length)
                {
                    // Optional independent engine, already used by driver tests.
                    // All generation, expected values and reduction stay in C.
                    run[run_count++] = context->node;
                    if (context->node_memory64_flag)
                        run[run_count++] = S8("--experimental-wasm-memory64");
                    run[run_count++] = S8("-e");
                    run[run_count++] = S8("const fs=require('fs');const b=fs.readFileSync(process.argv[1]);"
                                         "const m=new WebAssembly.Module(b);const i=new WebAssembly.Instance(m);process.exit(i.exports.main());");
                    run[run_count++] = artifact;
                }
                else if (target.backend == META_NATIVE && target.os == OPERATING_SYSTEM_LINUX &&
                         target.arch == CPU_ARCH_AARCH64 && context->qemu.length)
                { run[run_count++] = context->qemu; run[run_count++] = executable; }
                else if (target.backend == META_NATIVE && target.os == OPERATING_SYSTEM_WINDOWS &&
                         target.arch == CPU_ARCH_X86_64 && context->wine.length)
                { run[run_count++] = context->wine; run[run_count++] = executable; }
                if (run_count)
                {
                    result = meta_process(arena, (SliceString8){run, run_count}, META_PHASE_EXECUTE);
                    result.command = string_format(arena, S8("{S8}\n{S8}"), compile_command, result.command);
                }
                else if (target.backend == META_EBPF)
                {
                    ByteSlice object = file_read(arena, artifact, (FileReadOptions){0});
                    bool valid = true;
                    bool interpreted = true;
                    u32 failing_input = 0;
                    u64 observed = 0, expected = 0;
                    for (u32 input = 0; valid && input < spec.input_count; input += 1)
                    {
                        interpreted = codegen_test_ebpf_execute(object, meta_inputs[input][0], meta_inputs[input][1], &observed);
                        expected = meta_expected(spec, meta_inputs[input][0], meta_inputs[input][1]);
                        valid = interpreted && observed == expected;
                        if (!valid)
                        {
                            failing_input = input + 1;
                        }
                    }
                    result = (MetaOutcome){.phase = interpreted ? META_PHASE_EXECUTE : META_PHASE_RUNNER, .launched = true, .command = compile_command,
                                           .wait = {.result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED, .platform_status = failing_input}};
                    if (!valid)
                    {
                        String8 message = string_format(arena, S8("eBPF input={u32} interpreted={u32} actual={u64} expected={u64}\n"),
                                                         failing_input, (u32)interpreted, observed, expected);
                        result.wait.streams[STANDARD_STREAM_ERROR] = BUSTER_SLICE_TO_BYTE_SLICE(message);
                    }
                }
                else result.phase = META_PHASE_UNEXECUTED;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool meta_same_stream(ByteSlice a, ByteSlice b)
{
    return a.length == b.length && (!a.length || memcmp(a.pointer, b.pointer, (size_t)a.length) == 0);
}

BUSTER_GLOBAL_LOCAL MetaPair meta_assess(MetaOutcome base, MetaOutcome changed)
{
    MetaPair result = {.base = base, .changed = changed};
    result.executed = result.base.phase == META_PHASE_EXECUTE && result.changed.phase == META_PHASE_EXECUTE;
    result.passed = meta_success(result.base) && meta_success(result.changed) && result.base.phase == result.changed.phase &&
                    result.base.wait.platform_status == result.changed.wait.platform_status &&
                    meta_same_stream(result.base.wait.streams[STANDARD_STREAM_OUTPUT], result.changed.wait.streams[STANDARD_STREAM_OUTPUT]) &&
                    meta_same_stream(result.base.wait.streams[STANDARD_STREAM_ERROR], result.changed.wait.streams[STANDARD_STREAM_ERROR]);
    return result;
}

BUSTER_GLOBAL_LOCAL MetaPair meta_pair(MetaContext* context, MetaSpec spec, u32 mask, MetaTarget target, u32 allocator, bool reference)
{
    // A completed image can remain locked briefly on Windows. Never compile a
    // later work item over either executable: the atomic claim also keeps the
    // naming contract safe if the case loop is split across lanes later.
    u64 work_index = atomic_u64_increment(&context->work_serial);
    String8 directory = string_format_z(context->arena, S8("{S8}/work-{u64}"), context->directory, work_index);
    os_make_directory(directory);
    MetaOutcome base = meta_execute(context, directory, spec, target, allocator, S8("base"), 0, reference);
    MetaOutcome changed = meta_execute(context, directory, spec, target, allocator, S8("transformed"), mask, reference);
    MetaPair result = meta_assess(base, changed);
    result.directory = directory;
    return result;
}

BUSTER_GLOBAL_LOCAL bool meta_same_failure_side(MetaOutcome a, MetaOutcome b)
{
    bool result = a.phase == b.phase && a.launched == b.launched && a.wait.result == b.wait.result &&
                  a.wait.timed_out == b.wait.timed_out && a.wait.platform_status == b.wait.platform_status;
    if (result && a.phase == META_PHASE_COMPILE && a.wait.result == PROCESS_RESULT_FAILED)
        result = string_equal(a.diagnostic, b.diagnostic);
    return result;
}

BUSTER_GLOBAL_LOCAL bool meta_same_failure(MetaPair a, MetaPair b)
{
    bool result = !a.passed && !b.passed && meta_same_failure_side(a.base, b.base) && meta_same_failure_side(a.changed, b.changed);
    if (result && a.executed && b.executed)
    {
        // Keep an output mismatch an output mismatch, not an unrelated exit-
        // status failure with coincidentally identical per-side statuses.
        for (u32 stream = STANDARD_STREAM_OUTPUT; stream <= STANDARD_STREAM_ERROR; stream += 1)
        {
            result &= meta_same_stream(a.base.wait.streams[stream], a.changed.wait.streams[stream]) ==
                      meta_same_stream(b.base.wait.streams[stream], b.changed.wait.streams[stream]);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void meta_save_outcome(Arena* arena, String8 directory, String8 side, MetaOutcome outcome)
{
    String8 report = string_format(arena,
        S8("phase={u32} launched={u32} result={u32} platform_status={u32} timed_out={u32}\n{S8}\nstdout:\n{S8}\nstderr:\n{S8}\n"),
        (u32)outcome.phase, (u32)outcome.launched, (u32)outcome.wait.result, outcome.wait.platform_status, (u32)outcome.wait.timed_out,
        outcome.command, (String8){(char8*)outcome.wait.streams[STANDARD_STREAM_OUTPUT].pointer, outcome.wait.streams[STANDARD_STREAM_OUTPUT].length},
        (String8){(char8*)outcome.wait.streams[STANDARD_STREAM_ERROR].pointer, outcome.wait.streams[STANDARD_STREAM_ERROR].length});
    String8 path = string_format_z(arena, S8("{S8}/{S8}.log"), directory, side);
    BUSTER_CHECK(file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(report)));
}

BUSTER_GLOBAL_LOCAL bool meta_reduction_accept(MetaContext* context, MetaSpec spec, u32 mask, MetaTarget target,
                                                 u32 allocator, MetaPair signature, u32* attempts)
{
    bool accepted = false;
    if (*attempts + 2 <= BUSTER_META_MAX_REDUCTIONS)
    {
        // Two matching replays reject flaky crashes and reducer-induced bugs.
        TemporalArena temporary = arena_begin_temporal(context->arena);
        MetaPair first = meta_pair(context, spec, mask, target, allocator, false);
        MetaPair second = meta_pair(context, spec, mask, target, allocator, false);
        *attempts += 2;
        accepted = meta_same_failure(signature, first) && meta_same_failure(signature, second);
        arena_set_position(context->arena, temporary.position);
    }
    return accepted;
}

BUSTER_GLOBAL_LOCAL u32 meta_reduce(MetaContext* context, MetaSpec* spec, u32* mask, MetaTarget target, u32 allocator, MetaPair signature)
{
    u32 attempts = 0;
    bool can_reduce = !signature.base.wait.timed_out && !signature.changed.wait.timed_out;
    for (u32 bit = 0; can_reduce && bit < BUSTER_META_TRANSFORM_COUNT; bit += 1)
    {
        u32 candidate = *mask & ~(1u << bit);
        if (candidate && candidate != *mask && meta_reduction_accept(context, *spec, candidate, target, allocator, signature, &attempts))
            *mask = candidate;
    }
    // These edits keep both sources well-formed and equivalent. A live local
    // remains even with no arithmetic/loop, so the rename relation survives.
    for (u32 field = 0; can_reduce && field < 5; field += 1)
    {
        MetaSpec candidate = *spec;
        switch (field)
        {
            case 0: candidate.terms = 0; break;
            case 1: candidate.rounds = 0; break;
            case 2: candidate.salt = 0; break;
            case 3: candidate.factor = 0; break;
            case 4: candidate.input_count = 1; break;
        }
        if (meta_reduction_accept(context, candidate, *mask, target, allocator, signature, &attempts))
        {
            *spec = candidate;
        }
    }
    return attempts;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool meta_number(String8 name, u32 fallback, u32 maximum, u32* output)
{
    String8 text = os_get_environment_variable(name);
    bool valid = true;
    u32 value = 0;
    if (!text.length)
    {
        value = fallback;
    }
    for (u64 index = 0; valid && index < text.length; index += 1)
    {
        char8 digit = text.pointer[index];
        valid = digit >= '0' && digit <= '9' && value <= maximum / 10;
        if (valid)
        {
            u32 next = (u32)(digit - '0');
            valid = value * 10 <= maximum && next <= maximum - value * 10;
            if (valid)
            {
                value = value * 10 + next;
            }
        }
    }
    if (valid)
    {
        *output = value;
    }
    else string_print(S8("METAMORPHIC invalid {S8}: {S8}\n"), name, text);
    return valid;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL MetaSummary meta_run(MetaContext* context, u32 first_seed, u32 cases, u32 transform_mask)
{
    MetaSummary result = {0};
    Arena* arena = context->arena;
    MetaPair signatures[BUSTER_META_MAX_SIGNATURES] = {0};
    u32 signature_targets[BUSTER_META_MAX_SIGNATURES] = {0};
    TimeDataType started = timestamp_take();
    u64 budget_ns = context->full || BUSTER_SANITIZE ? UINT64_C(300000000000) : UINT64_C(60000000000);
    bool budget_expired = false;
    for (u32 reference = 0; !budget_expired && reference < BUSTER_META_REFERENCE_COUNT; reference += 1)
    {
        context->reference_compiler = reference < BUSTER_META_REFERENCE_COUNT / 2 ? context->clang : context->gcc;
        context->reference_optimized = (reference & 1u) != 0;
        String8 optimization = context->reference_optimized ? S8("-O2") : S8("-O0");
        if (!context->reference_compiler.length)
        {
            if (context->full)
                string_print(S8("METAMORPHIC_REFERENCE_UNAVAILABLE compiler={S8} optimization={S8}\n"),
                             reference < BUSTER_META_REFERENCE_COUNT / 2 ? S8("clang") : S8("gcc"), optimization);
            continue;
        }
        u32 reference_start = result.reference_pairs;
        for (u32 seed_index = 0; !budget_expired && seed_index < cases; seed_index += 1)
        {
            for (u32 transform = 0; !budget_expired && transform <= BUSTER_META_TRANSFORM_COUNT; transform += 1)
            {
                u32 mask = transform == BUSTER_META_TRANSFORM_COUNT ? transform_mask : (1u << transform) & transform_mask;
                if (!mask || (transform == BUSTER_META_TRANSFORM_COUNT && !(mask & (mask - 1))))
                {
                    continue;
                }
                budget_expired = timestamp_ns_between(started, timestamp_take()) >= budget_ns;
                if (budget_expired)
                {
                    result.failures += 1;
                    string_print(S8("METAMORPHIC_BUDGET_EXHAUSTED reference_pairs={u32}\n"), result.reference_pairs);
                    break;
                }
                TemporalArena temporary = arena_begin_temporal(arena);
                MetaPair pair = meta_pair(context, meta_spec(first_seed + seed_index), mask, meta_targets[0], 0, true);
                result.reference_pairs += 1;
                if (!pair.passed)
                {
                    result.failures += 1;
                    string_print(S8("METAMORPHIC_REFERENCE_FAILURE seed={u32} mask={u32} compiler={S8} optimization={S8}\n"),
                                 first_seed + seed_index, mask, context->reference_compiler, optimization);
                    meta_save_outcome(arena, context->directory, S8("reference-base"), pair.base);
                    meta_save_outcome(arena, context->directory, S8("reference-transformed"), pair.changed);
                    // Do not promote invalid source/engine setup as a compiler
                    // regression. Leave this pair's sources intact for review.
                    budget_expired = true;
                }
                arena_set_position(arena, temporary.position);
            }
        }
        string_print(S8("METAMORPHIC_REFERENCE pairs={u32} failed={u32} compiler={S8} optimization={S8}\n"),
                     result.reference_pairs - reference_start, result.failures, context->reference_compiler, optimization);
    }
    for (u32 target_index = 0; !budget_expired && target_index < BUSTER_ARRAY_LENGTH(meta_targets); target_index += 1)
    {
        MetaTarget target = meta_targets[target_index];
        if (!context->full && !meta_native_target(target))
        {
            continue;
        }
        if (context->target_filter.length && !string_equal(context->target_filter, target.name))
        {
            continue;
        }
        u32 target_mask = meta_target_transform_mask(target, transform_mask);
        if (target_mask != transform_mask)
            string_print(S8("METAMORPHIC_TRANSFORMS_UNAVAILABLE target={S8} mask={u32} reason=local-calls-not-interpreted\n"),
                         target.name, transform_mask & ~target_mask);
        for (u32 mode = 0; !budget_expired && mode < CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT; mode += 1)
        {
            u32 row_pairs = 0, row_failures = 0, row_executed = 0, row_unexecuted = 0;
            for (u32 seed_index = 0; !budget_expired && seed_index < cases; seed_index += 1)
            {
                MetaSpec spec = meta_spec(first_seed + seed_index);
                for (u32 transform = 0; !budget_expired && transform <= BUSTER_META_TRANSFORM_COUNT; transform += 1)
                {
                    u32 mask = transform == BUSTER_META_TRANSFORM_COUNT ? target_mask : (1u << transform) & target_mask;
                    if (!mask || (transform == BUSTER_META_TRANSFORM_COUNT && !(mask & (mask - 1))))
                    {
                        continue;
                    }
                    budget_expired = timestamp_ns_between(started, timestamp_take()) >= budget_ns;
                    if (budget_expired)
                    {
                        result.failures += 1;
                        string_print(S8("METAMORPHIC_BUDGET_EXHAUSTED completed_pairs={u32}\n"), result.pairs);
                        break;
                    }
                    TemporalArena temporary = arena_begin_temporal(arena);
                    MetaPair pair = meta_pair(context, spec, mask, target, mode, false);
                    row_pairs += 1;
                    result.pairs += 1;
                    result.passed += pair.passed;
                    row_executed += pair.executed;
                    result.executed += pair.executed;
                    bool unexecuted = pair.passed && !pair.executed;
                    row_unexecuted += unexecuted;
                    result.unexecuted += unexecuted;
                    if (!pair.passed)
                    {
                        row_failures += 1;
                        result.failures += 1;
                        bool duplicate = false;
                        for (u32 index = 0; index < result.unique_failures; index += 1)
                            duplicate |= signature_targets[index] == target_index && meta_same_failure(signatures[index], pair);
                        if (!duplicate && result.unique_failures < BUSTER_META_MAX_SIGNATURES)
                        {
                            u32 unique = result.unique_failures++;
                            String8 original_directory = context->directory;
                            String8 failure = string_format_z(arena, S8("{S8}/failure-{u32}"), original_directory, unique);
                            os_make_directory(failure);
                            meta_save_outcome(arena, failure, S8("observed-base"), pair.base);
                            meta_save_outcome(arena, failure, S8("observed-transformed"), pair.changed);
                            String8 sides[] = {S8("base"), S8("transformed")};
                            String8 extensions[] = {S8("c"), S8("artifact"), S8("exe")};
                            for (u32 side = 0; side < BUSTER_ARRAY_LENGTH(sides); side += 1)
                            {
                                for (u32 extension = 0; extension < BUSTER_ARRAY_LENGTH(extensions); extension += 1)
                                {
                                    String8 observed = string_format_z(arena, S8("{S8}/{S8}.{S8}"), pair.directory, sides[side], extensions[extension]);
                                    String8 saved = string_format_z(arena, S8("{S8}/observed-{S8}.{S8}"), failure, sides[side], extensions[extension]);
                                    if (meta_output_present(observed, false) && !file_copy((CopyFileArguments){observed, saved}))
                                        string_print(S8("METAMORPHIC observed artifact could not be saved: {S8}\n"), observed);
                                }
                            }
                            context->directory = failure;
                            MetaPair replay = meta_pair(context, spec, mask, target, mode, false);
                            meta_save_outcome(arena, failure, S8("base"), replay.base);
                            meta_save_outcome(arena, failure, S8("transformed"), replay.changed);
                            MetaSpec reduced = spec;
                            u32 reduced_mask = mask;
                            context->directory = original_directory;
                            u32 reductions = meta_reduce(context, &reduced, &reduced_mask, target, mode, pair);
                            result.reductions += reductions;
                            String8 minimized = string_format_z(arena, S8("{S8}/minimized"), failure);
                            os_make_directory(minimized);
                            context->directory = minimized;
                            MetaPair final = meta_pair(context, reduced, reduced_mask, target, mode, false);
                            meta_save_outcome(arena, minimized, S8("base"), final.base);
                            meta_save_outcome(arena, minimized, S8("transformed"), final.changed);
                            context->directory = original_directory;
                            String8 report = string_format(arena,
                                S8("seed={u32} target={S8} allocator={S8} mask={u32}\nminimized_mask={u32} terms={u32} rounds={u32} salt={u32} factor={u32} inputs={u32}\n"
                                   "reducer_replays={u32} signature_preserved={u32}\ncompiler={S8}\n"),
                                spec.seed, target.name, codegen_register_allocator_mode_string((CodegenRegisterAllocatorMode)mode), mask,
                                reduced_mask, reduced.terms, reduced.rounds, reduced.salt, reduced.factor, reduced.input_count, reductions,
                                (u32)meta_same_failure(pair, final), context->compiler);
                            String8 report_path = string_format_z(arena, S8("{S8}/reproducer.txt"), failure);
                            BUSTER_CHECK(file_write(report_path, BUSTER_SLICE_TO_BYTE_SLICE(report)));
                            string_print(S8("METAMORPHIC_FAILURE seed={u32} mask={u32} target={S8} allocator={u32} bundle={S8}\n"),
                                         spec.seed, mask, target.name, mode, failure);
                            // Keep signature storage below the next rewind.
                            signatures[unique] = pair;
                            signature_targets[unique] = target_index;
                            temporary.position = arena->position;
                        }
                    }
                    arena_set_position(arena, temporary.position);
                }
            }
            string_print(S8("METAMORPHIC target={S8} allocator={S8} pairs={u32} executed={u32} unexecuted={u32} failed={u32}\n"),
                         target.name, codegen_register_allocator_mode_string((CodegenRegisterAllocatorMode)mode), row_pairs,
                         row_executed, row_unexecuted, row_failures);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL MetaContext meta_context(Arena* arena, String8 directory, bool full)
{
    String8 compiler = os_get_environment_variable(S8("BUSTER_METAMORPHIC_COMPILER"));
    if (!compiler.length && program_state && program_state->input.arguments.length)
    {
        compiler = program_state->input.arguments.pointer[0];
    }
    MetaContext result = {.arena = arena, .directory = directory, .compiler = compiler, .full = full, .frontend_ssa = true};
    if (full)
    {
        result.target_filter = os_get_environment_variable(S8("BUSTER_METAMORPHIC_TARGET"));
        result.clang = executable_resolve_in_path(arena, S8("clang"));
        result.gcc = executable_resolve_in_path(arena, S8("gcc"));
        result.node = executable_resolve_in_path(arena, S8("node"));
        result.qemu = executable_resolve_in_path(arena, S8("qemu-aarch64"));
        result.wine = executable_resolve_in_path(arena, S8("wine"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool meta_prepare_node(MetaContext* context)
{
    bool result = true;
    if (context->node.length && (!context->target_filter.length || string_equal(context->target_filter, S8("wasm64"))))
    {
        // Validate a fixed Memory64 module independently of generated source.
        // New engines enable it by default and may reject the former flag.
        String8 probe = S8("new WebAssembly.Module(new Uint8Array([0,97,115,109,1,0,0,0,5,3,1,4,0]))");
        String8 plain[] = {context->node, S8("-e"), probe};
        MetaOutcome normal = meta_process(context->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(plain), META_PHASE_RUNNER);
        if (!meta_success(normal))
        {
            String8 flagged[] = {context->node, S8("--experimental-wasm-memory64"), S8("-e"), probe};
            MetaOutcome experimental = meta_process(context->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(flagged), META_PHASE_RUNNER);
            context->node_memory64_flag = meta_success(experimental);
            result = context->node_memory64_flag;
            if (!result)
                string_print(S8("METAMORPHIC Node Memory64 runner setup failed\nwithout flag: {S8}\nwith flag: {S8}\n"),
                             normal.diagnostic, experimental.diagnostic);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult meta_preprocessor_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct PasteCase { String8 source; String8 expected; } cases[] = {
        {S8("#define CAT(a,b) a ## b\nCAT(,renamed)\n"), S8("renamed")},
        {S8("#define CAT(a,b) a ## b\nCAT(renamed,)\n"), S8("renamed")},
        {S8("#define CAT(a,b) a ## b\nCAT(,)\n"), S8("")},
        {S8("#define M(a,b) 1 + a ## b\nM(,2)\n"), S8("1 + 2")},
        {S8("#define M(a,b,c) a ## b ## c\nM(,,x) M(,x,) M(x,,) M(,,) M(,x,y) M(x,,y) M(x,y,) M(x,y,z)\n"), S8("x x x xy xy xy xyz")},
        {S8("#define M(a,...) a , ## __VA_ARGS__\nM(1) M(1,2)\n"), S8("1 1 , 2")},
        {S8("#define M(a,...) , ## a\nM(,unused)\n"), S8(",")},
        {S8("#define CAT(a,b) a ## b\n#define STR_(a) #a\n#define STR(a) STR_(a)\nSTR(CAT(,renamed))\n"), S8("\"renamed\"")},
        {S8("#define M(a,b) first a ## b\n#define STR_(a) #a\n#define STR(a) STR_(a)\nSTR(M(,next))\n"), S8("\"first next\"")},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CPreprocessResult actual = c_preprocess(arguments->arena, cases[index].source, (CPreprocessOptions){0});
        CPreprocessResult expected = c_preprocess(arguments->arena, cases[index].expected, (CPreprocessOptions){0});
        bool equal = !actual.error_count && !expected.error_count && actual.token_count == expected.token_count;
        for (u64 token = 0; equal && token < actual.token_count; token += 1)
            equal = actual.tokens[token].kind == expected.tokens[token].kind &&
                    string_equal(c_token_spelling(actual.spelling_base, actual.tokens[token]), c_token_spelling(expected.spelling_base, expected.tokens[token]));
        BUSTER_TEST(arguments, equal);
        arena_set_position(arguments->arena, temporary.position);
    }
    String8 invalid[] = {S8("#define M(a) ## a\nM(x)\n"), S8("#define M(a) a ##\nM(x)\n"), S8("#define M(a,b) a ## b\nM(+,2)\n")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CPreprocessResult parsed = c_preprocess(arguments->arena, invalid[index], (CPreprocessOptions){0});
        BUSTER_TEST(arguments, parsed.error_count != 0);
        arena_set_position(arguments->arena, temporary.position);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult meta_generator_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 seeds[] = {0, 1, 42, UINT32_MAX};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(seeds); index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        MetaSpec spec = meta_spec(seeds[index]);
        String8 base = meta_source(arguments->arena, spec, 0, false);
        BUSTER_TEST(arguments, !string_equal(base, meta_source(arguments->arena, spec, BUSTER_META_AGGREGATES, false)));
        BUSTER_TEST(arguments, !string_equal(base, meta_source(arguments->arena, spec, BUSTER_META_OUTLINE, false)));
        for (u32 transform = 0; transform <= BUSTER_META_TRANSFORM_COUNT; transform += 1)
        {
            u32 mask = transform == BUSTER_META_TRANSFORM_COUNT ? BUSTER_META_ALL_TRANSFORMS : 1u << transform;
            String8 first = meta_source(arguments->arena, spec, mask, false);
            String8 second = meta_source(arguments->arena, meta_spec(seeds[index]), mask, false);
            BUSTER_TEST(arguments, string_equal(first, second));
        }
        // Invalid token pastes are diagnostic-only inputs, never executables.
        // A valid adjacent-token control prevents universal rejection passing.
        String8 valid = string_format(arguments->arena,
            S8("#define JOIN(a,b) a ## b\nint JOIN(v,{u32})(void) {{ return 0; }}\n"), seeds[index]);
        String8 invalid = string_format(arguments->arena,
            S8("#define JOIN(a,b) a ## b\nint f(void) {{ return JOIN(+,{u32}); }}\n"), seeds[index]);
        for (u32 layout = 0; layout < 2; layout += 1)
        {
            CPreprocessResult accepted = c_preprocess(arguments->arena, layout ? meta_layout(arguments->arena, valid) : valid, (CPreprocessOptions){0});
            CPreprocessResult rejected = c_preprocess(arguments->arena, layout ? meta_layout(arguments->arena, invalid) : invalid, (CPreprocessOptions){0});
            BUSTER_TEST(arguments, accepted.error_count == 0);
            BUSTER_TEST(arguments, rejected.error_count != 0);
        }
        arena_set_position(arguments->arena, temporary.position);
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(meta_targets); index += 1)
    {
        u32 expected = meta_targets[index].backend == META_EBPF ? BUSTER_META_ALL_TRANSFORMS & ~BUSTER_META_OUTLINE : BUSTER_META_ALL_TRANSFORMS;
        BUSTER_TEST(arguments, meta_target_transform_mask(meta_targets[index], BUSTER_META_ALL_TRANSFORMS) == expected);
    }
    return result;
}

UnitTestResult metamorphic_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = meta_preprocessor_tests(arguments);
    UnitTestResult generated = meta_generator_tests(arguments);
    result.succeeded_test_count += generated.succeeded_test_count;
    result.test_count += generated.test_count;
    MetaOutcome ok = {.phase = META_PHASE_EXECUTE, .launched = true, .wait = {.result = PROCESS_RESULT_SUCCESS}};
    MetaOutcome bad = ok;
    bad.wait.result = PROCESS_RESULT_FAILED;
    bad.wait.platform_status = 7;
    BUSTER_TEST(arguments, meta_assess(ok, ok).passed);
    BUSTER_TEST(arguments, !meta_assess(ok, bad).passed);
    BUSTER_TEST(arguments, !meta_assess(bad, bad).passed);
    bad = ok;
    bad.wait.timed_out = 1;
    BUSTER_TEST(arguments, !meta_assess(bad, bad).passed);
    bad = ok;
    bad.wait.streams[STANDARD_STREAM_OUTPUT] = (ByteSlice){(u8*)"different", 9};
    BUSTER_TEST(arguments, !meta_assess(ok, bad).passed);
    MetaPair output_mismatch = meta_assess(ok, bad);
    bad = ok;
    bad.wait.streams[STANDARD_STREAM_ERROR] = (ByteSlice){(u8*)"different", 9};
    BUSTER_TEST(arguments, !meta_assess(ok, bad).passed);
    BUSTER_TEST(arguments, !meta_same_failure(output_mismatch, meta_assess(ok, bad)));
    bad = ok;
    bad.wait.platform_status = 1;
    BUSTER_TEST(arguments, !meta_assess(ok, bad).passed);
    BUSTER_TEST(arguments, string_equal(meta_diagnostic(S8("cc: error: before.c:1:2: bad token\n")),
                                       meta_diagnostic(S8("cc: error: after.c:10:20: bad token\n"))));
    BUSTER_TEST(arguments, meta_expected((MetaSpec){.terms = 1, .rounds = 2, .salt = 3, .factor = 5}, 2, 7) == 79);
#if BUSTER_LINK_LIBC && !BUSTER_ANDROID && !BUSTER_IOS
    String8 directory = buster_test_temporary_path(arguments->arena, S8("metamorphic"), S8(""));
    os_make_directory(directory);
    String8 stale = string_format_z(arguments->arena, S8("{S8}/stale-output"), directory);
    BUSTER_TEST(arguments, file_write(stale, (ByteSlice){(u8*)"old executable", 14}));
    BUSTER_TEST(arguments, meta_clear_output(stale));
    BUSTER_TEST(arguments, !meta_success(meta_require_output(ok, stale)));
    BUSTER_TEST(arguments, file_write(stale, (ByteSlice){0}));
    BUSTER_TEST(arguments, !meta_success(meta_require_output(ok, stale)));
    BUSTER_TEST(arguments, meta_clear_output(stale));
    MetaContext context = meta_context(arguments->arena, directory, false);
    MetaSummary summary = meta_run(&context, 1, 1, BUSTER_META_ALL_TRANSFORMS);
    BUSTER_TEST(arguments, summary.failures == 0);
    BUSTER_TEST(arguments, summary.executed == (BUSTER_META_TRANSFORM_COUNT + 1) * CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT);
    u64 work_count = atomic_u64_add(&context.work_serial, 0);
    BUSTER_TEST(arguments, work_count == summary.pairs);
    String8 first_executable = string_format_z(arguments->arena, S8("{S8}/work-0/base.exe"), directory);
    String8 last_executable = string_format_z(arguments->arena, S8("{S8}/work-{u64}/base.exe"), directory, work_count - 1);
    BUSTER_TEST(arguments, !string_equal(first_executable, last_executable));
    BUSTER_TEST(arguments, meta_output_present(first_executable, true));
    BUSTER_TEST(arguments, meta_output_present(last_executable, true));
#else
    arguments->show(arguments, S8("METAMORPHIC execution unavailable on this platform; preprocessing regressions executed\n"));
#endif
    return result;
}

ProcessResult metamorphic_campaign(Arena* arena)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINK_LIBC && !BUSTER_ANDROID && !BUSTER_IOS
    u32 seed = 1, cases = 4, mask = BUSTER_META_ALL_TRANSFORMS, require_execution = 0, frontend_ssa = 1;
    bool valid = meta_number(S8("BUSTER_METAMORPHIC_SEED"), 1, UINT32_MAX, &seed) &&
                 meta_number(S8("BUSTER_METAMORPHIC_CASES"), 4, BUSTER_META_MAX_CASES, &cases) && cases &&
                 meta_number(S8("BUSTER_METAMORPHIC_TRANSFORMS"), BUSTER_META_ALL_TRANSFORMS, BUSTER_META_ALL_TRANSFORMS, &mask) && mask &&
                 meta_number(S8("BUSTER_METAMORPHIC_REQUIRE_EXECUTION"), 0, 1, &require_execution) &&
                 meta_number(S8("BUSTER_METAMORPHIC_FRONTEND_SSA"), 1, 1, &frontend_ssa);
    if (valid)
    {
        String8 directory = os_get_environment_variable(S8("BUSTER_METAMORPHIC_OUTPUT"));
        if (!directory.length)
        {
            directory = string_format_z(arena, S8("build/metamorphic-{u64}"), os_get_current_process_id());
        }
        os_make_directory(directory);
        directory = os_path_absolute(arena, directory, true);
        String8 probe = directory.length ? string_format_z(arena, S8("{S8}/campaign.txt"), directory) : (String8){0};
        String8 probe_text = S8("Buster metamorphic campaign. See METAMORPHIC_SUMMARY for actual coverage.\n");
        if (!directory.length || !file_write(probe, BUSTER_SLICE_TO_BYTE_SLICE(probe_text)))
        {
            string_print(S8("METAMORPHIC output directory could not be created/resolved/written\n"));
        }
        else
        {
            MetaContext context = meta_context(arena, directory, true);
            context.frontend_ssa = frontend_ssa != 0;
            bool target_valid = !context.target_filter.length;
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(meta_targets); index += 1)
                target_valid |= string_equal(context.target_filter, meta_targets[index].name);
            if (!target_valid)
            {
                string_print(S8("METAMORPHIC unknown target filter: {S8}\n"), context.target_filter);
            }
            else if (meta_prepare_node(&context))
            {
                MetaSummary summary = meta_run(&context, seed, cases, mask);
                string_print(S8("METAMORPHIC_SUMMARY seed={u32} cases={u32} pairs={u32} executed={u32} unexecuted={u32} failures={u32} unique={u32} reducer_replays={u32} frontend_ssa={u32} output={S8}\n"),
                             seed, cases, summary.pairs, summary.executed, summary.unexecuted, summary.failures, summary.unique_failures, summary.reductions, frontend_ssa, directory);
                if (require_execution && summary.unexecuted)
                    string_print(S8("METAMORPHIC required execution unavailable for {u32} pairs\n"), summary.unexecuted);
                // Compile-only rows are not execution passes. Strict mode
                // requires runners, rather than treating their absence as green.
                result = summary.failures || !summary.pairs || (require_execution && summary.unexecuted) ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
            }
        }
    }
    else string_print(S8("METAMORPHIC case count and transformation mask must be nonzero\n"));
#else
    BUSTER_UNUSED(arena);
    string_print(S8("METAMORPHIC campaign requires a desktop process runner\n"));
#endif
    return result;
}
#endif
