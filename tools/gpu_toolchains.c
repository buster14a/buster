// Real GPU ecosystem acceptance, included by build.c after differential.c.
// gpu_tools_main owns strict profile selection; gpu_tools_profile executes real
// ide pipelines and independent consumers. Reuse d_observe/d_write/d_create_output
// for bounded processes, lossless argv and diagnostics, and exclusive evidence
// directories. No shader SDK is needed by the build driver or planner tests.
#include "throughput/hash.h"

#define GPU_TOOLS_PROFILE_COUNT 5
#define GPU_TOOLS_RUN(settings, name, ...) gpu_tools_run(settings, name, (String8[]){__VA_ARGS__}, sizeof((String8[]){__VA_ARGS__}) / sizeof(String8))

BUSTER_GLOBAL_LOCAL String8 const gpu_tools_profiles[] = {
    S8_INITIALIZER("spirv-dxc-2025.07"), S8_INITIALIZER("ptx-llvm18-cuda12.4"),
    S8_INITIALIZER("amdgcn-llvm18"), S8_INITIALIZER("metal-xcode16.4"), S8_INITIALIZER("dxil-dxc-2025.07")};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(gpu_tools_profiles) == GPU_TOOLS_PROFILE_COUNT);

typedef struct GpuToolsFixture GpuToolsFixture;
struct GpuToolsFixture { String8 path; String8 sha256; };
BUSTER_GLOBAL_LOCAL GpuToolsFixture const gpu_tools_fixtures[] = {
    {S8_INITIALIZER("tests/gpu/smoke.hlsl"), S8_INITIALIZER("510fa503ae77a5fb29dfbad113e4e24c6c556377bf80bf36ccae66fa3add0dd8")},
    {S8_INITIALIZER("tests/gpu/smoke.ll"), S8_INITIALIZER("0c294c24d6de71b8dbcce99ac6b881bb03adc0286c8dc1dc8e40a84c662f5106")},
    {S8_INITIALIZER("tests/gpu/smoke.cl"), S8_INITIALIZER("4af41171a25c6b0a93e405910ac2f2f9035414fb74f91c262c7350d67d8f75ac")},
    {S8_INITIALIZER("tests/gpu/smoke.metal"), S8_INITIALIZER("f519a92229ec74c8841b772f173a79a5481503330660bf6a182f07f1b302b7e7")},
    {S8_INITIALIZER("tests/gpu/metal_reader.c"), S8_INITIALIZER("a9013aae35ebfed695f4a4cca4dcb3ee6a78e4cee86091efafd5ea6cfe87ef0b")}};

typedef struct GpuTools GpuTools;
struct GpuTools
{
    DSettings evidence;
    String8 directory;
    String8 compiler, llc, readobj, objdump, dxc, dxv, spirv_val, ptxas, xcrun, xcodebuild;
    u32 command_count;
    u32 hash_count;
};

BUSTER_GLOBAL_LOCAL String8 gpu_tools_path(Arena* arena, String8 variable, String8 fallback)
{
    String8 configured = os_get_environment_variable(variable);
    String8 path = configured.length ? configured : fallback;
    String8 resolved = executable_resolve_in_path(arena, path);
    return resolved.length ? resolved : path;
}

BUSTER_GLOBAL_LOCAL bool gpu_tools_hash(GpuTools* runner, String8 path, String8 expected)
{
    DSettings* settings = &runner->evidence;
    String8 terminated = string_duplicate_arena(settings->arena, path, true);
    char digest[65] = {0};
    uint64_t bytes = 0, lines = 0;
    bool ok = tp_hash_file((char*)terminated.pointer, digest, &bytes, &lines) && bytes > 0;
    String8 actual = {.pointer = (char8*)digest, .length = 64};
    if (expected.length) { ok &= string_equal(actual, expected); }
    d_write(settings, path_join(settings->arena, settings->out,
        string_format(settings->arena, S8("hash-{u32}.txt"), runner->hash_count++)),
        string_format(settings->arena, S8("sha256={S8}\nbytes={u64}\npath={S8}\nmatch={u32}\n"), actual, (u64)bytes, path, (u32)ok));
    if (!ok) { string_print(S8("GPU_HASH_FAIL {S8}\n"), path); }
    return ok && !settings->io_failed;
}

BUSTER_GLOBAL_LOCAL DObservation gpu_tools_run(GpuTools* settings, String8 name, String8* argv, u64 count)
{
    String8 prefix = path_join(settings->evidence.arena, settings->directory,
        string_format(settings->evidence.arena, S8("{u32}-{S8}"), settings->command_count++, name));
    DObservation result = d_observe(&settings->evidence, (SliceString8){.pointer = argv, .length = count}, prefix);
    if (!d_success(result)) { string_print(S8("GPU_COMMAND {S8} kind={u32} status={u32}\n"), prefix, (u32)result.kind, result.status); }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gpu_tools_version(GpuTools* settings, String8 tool, String8 version)
{
    DObservation result = GPU_TOOLS_RUN(settings, S8("version"), tool, S8("--version"));
    bool ok = d_success(result) && (d_contains(result.output, version) || d_contains(result.error, version));
    ok &= gpu_tools_hash(settings, tool, (String8){0});
    if (!ok) { string_print(S8("GPU_VERSION_FAIL {S8}: expected {S8}\n"), tool, version); }
    return ok;
}

// Require a normal nonzero exit with a diagnostic, never spawn errors, signals
// or timeouts. The preceding positive consumption must also have succeeded.
BUSTER_GLOBAL_LOCAL bool gpu_tools_rejected(DObservation observation)
{
    return d_normal(observation) && observation.status != 0 && (observation.output.length || observation.error.length);
}

BUSTER_GLOBAL_LOCAL bool gpu_tools_profile(GpuTools* settings, u32 profile)
{
    DSettings* evidence = &settings->evidence;
    Arena* arena = evidence->arena;
    settings->directory = path_join(arena, evidence->out, gpu_tools_profiles[profile]);
    bool ok = d_create_output(arena, settings->directory);
    String8 artifact = path_join(arena, settings->directory, S8("artifact"));
    String8 invalid = path_join(arena, settings->directory, S8("invalid"));
    String8 source = gpu_tools_fixtures[profile == 4 ? 0 : profile].path;
    String8 malformed = S8("DXBC\0\0\0\0\0\0\0\0\0\0\0\0");
    DObservation compiled = {.kind = D_WAIT}, consumed = {.kind = D_WAIT}, rejected = {.kind = D_WAIT};
    if (profile == 0 || profile == 4)
    {
        ok &= gpu_tools_version(settings, settings->dxc, S8("b106a961"));
        if (profile == 0) { ok &= gpu_tools_version(settings, settings->spirv_val, S8("SPIRV-Tools v2025.1")); }
        else
        {
            // dxv has no version switch. DXC reports the companion libdxil
            // version; record dxv's executable hash and its help separately.
            ok &= gpu_tools_hash(settings, settings->dxv, (String8){0});
            ok &= d_success(GPU_TOOLS_RUN(settings, S8("dxv-help"), settings->dxv, S8("-help")));
        }
        if (ok)
        {
            compiled = GPU_TOOLS_RUN(settings, S8("compile"), evidence->ide, S8("cc"),
                profile == 0 ? S8("--target=spirv-unknown-vulkan1.2") : S8("--target=dxil-pc-shadermodel6.0-compute"),
                S8("--shader-model=6.0"), S8("--gpu-entry=main"), S8("--dxc"), settings->dxc, source, S8("-o"), artifact);
            if (profile == 0)
            {
                // Valid SPIR-V header, no memory model or entry point.
                malformed = S8("\x03\x02\x23\x07\x00\x00\x01\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00");
                consumed = GPU_TOOLS_RUN(settings, S8("validate"), settings->spirv_val, S8("--target-env"), S8("vulkan1.2"), artifact);
                d_write(evidence, invalid, malformed);
                rejected = GPU_TOOLS_RUN(settings, S8("reject"), settings->spirv_val, S8("--target-env"), S8("vulkan1.2"), invalid);
            }
            else
            {
                String8 validated = path_join(arena, settings->directory, S8("validated.dxil"));
                consumed = GPU_TOOLS_RUN(settings, S8("validate"), settings->dxv, artifact, string_format(arena, S8("-o={S8}"), validated));
                ok &= gpu_tools_hash(settings, validated, (String8){0});
                d_write(evidence, invalid, malformed);
                rejected = GPU_TOOLS_RUN(settings, S8("reject"), settings->dxv, invalid);
                DObservation listing = GPU_TOOLS_RUN(settings, S8("read"), settings->dxc, S8("-dumpbin"), artifact);
                ok &= d_success(listing) && d_contains(listing.output, S8("main"));
            }
        }
    }
    else if (profile == 1)
    {
        ok &= gpu_tools_version(settings, settings->llc, S8("version 18.1."));
        ok &= gpu_tools_version(settings, settings->ptxas, S8("V12.4.131"));
        if (ok)
        {
            compiled = GPU_TOOLS_RUN(settings, S8("compile"), evidence->ide, S8("cc"), S8("--target=nvptx64-nvidia-cuda"),
                S8("--gpu-arch=sm_70"), S8("--gpu-llc"), settings->llc, source, S8("-o"), artifact);
            String8 cubin = path_join(arena, settings->directory, S8("accepted.cubin"));
            consumed = GPU_TOOLS_RUN(settings, S8("assemble"), settings->ptxas, S8("-v"), S8("-arch=sm_70"), artifact, S8("-o"), cubin);
            ok &= d_contains(consumed.error, S8("buster_gpu_smoke")) || d_contains(consumed.output, S8("buster_gpu_smoke"));
            ok &= gpu_tools_hash(settings, cubin, (String8){0});
            malformed = S8(".version 6.0\n.target sm_70\n.address_size 64\n.visible .entry bad() { invalid_instruction; }\n");
            d_write(evidence, invalid, malformed);
            rejected = GPU_TOOLS_RUN(settings, S8("reject"), settings->ptxas, S8("-arch=sm_70"), invalid,
                S8("-o"), path_join(arena, settings->directory, S8("rejected.cubin")));
        }
    }
    else if (profile == 2)
    {
        ok &= gpu_tools_version(settings, settings->compiler, S8("version 18.1."));
        ok &= gpu_tools_version(settings, settings->readobj, S8("version 18.1."));
        ok &= gpu_tools_version(settings, settings->objdump, S8("version 18.1."));
        // LLVM 18 AMDGPU uses GetProgramPath("ld.lld"), not --ld-path.
        // -B supplies the directory containing the exact probed ld.lld.
        String8 linker = gpu_tools_path(arena, S8("BUSTER_GPU_TEST_LLD"), S8("/usr/lib/llvm-18/bin/ld.lld"));
        ok &= string_ends_with_sequence(linker, S8("/ld.lld"));
        ok &= gpu_tools_version(settings, linker, S8("LLD 18.1."));
        if (ok)
        {
            String8 object = path_join(arena, settings->directory, S8("kernel.o"));
            DObservation object_compile = GPU_TOOLS_RUN(settings, S8("compile-object"), evidence->ide, S8("cc"), S8("--target=amdgcn-amd-amdhsa"),
                S8("--gpu-arch=gfx900"), S8("--gpu-clang"), settings->compiler, S8("-c"), source, S8("-o"), object);
            DObservation object_read = GPU_TOOLS_RUN(settings, S8("read-object"), settings->readobj, S8("--file-headers"), S8("--symbols"), S8("--notes"), object);
            ok &= d_success(object_compile) && d_success(object_read) && d_contains(object_read.output, S8("Relocatable")) &&
                d_contains(object_read.output, S8("buster_gpu_smoke")) && gpu_tools_hash(settings, object, (String8){0});
            compiled = GPU_TOOLS_RUN(settings, S8("compile"), evidence->ide, S8("cc"), S8("--target=amdgcn-amd-amdhsa"),
                S8("--gpu-arch=gfx900"), S8("--gpu-clang"), settings->compiler,
                string_format(arena, S8("-Xgpu=-B{S8}"), path_parent(arena, linker)), source, S8("-o"), artifact);
            consumed = GPU_TOOLS_RUN(settings, S8("read-hsa"), settings->readobj, S8("--file-headers"), S8("--symbols"), S8("--notes"), artifact);
            ok &= d_contains(consumed.output, S8("SharedObject")) && d_contains(consumed.output, S8("EM_AMDGPU")) &&
                d_contains(consumed.output, S8("amdhsa.kernels")) && d_contains(consumed.output, S8("buster_gpu_smoke"));
            DObservation assembly = GPU_TOOLS_RUN(settings, S8("disassemble"), settings->objdump, S8("--disassemble"), S8("--mcpu=gfx900"), artifact);
            ok &= d_success(assembly) && d_contains(assembly.output, S8("s_endpgm")) && !d_contains(assembly.output, S8("<unknown>"));
            ByteSlice bytes = file_read(arena, artifact, (FileReadOptions){0});
            if (bytes.length >= 64) { malformed = (String8){.pointer = (char8*)bytes.pointer, .length = 64}; }
            else { ok = false; }
            d_write(evidence, invalid, malformed);
            rejected = GPU_TOOLS_RUN(settings, S8("reject"), settings->readobj, S8("--file-headers"), S8("--symbols"), S8("--notes"), invalid);
        }
    }
    else
    {
        DObservation xcode = GPU_TOOLS_RUN(settings, S8("xcode-version"), settings->xcodebuild, S8("-version"));
        ok &= d_success(xcode) && d_contains(xcode.output, S8("Xcode 16.4\n"));
        ok &= d_success(GPU_TOOLS_RUN(settings, S8("metal-version"), settings->xcrun, S8("-sdk"), S8("macosx"), S8("metal"), S8("--version")));
        if (ok)
        {
            String8 air = path_join(arena, settings->directory, S8("kernel.air"));
            String8 reader = path_join(arena, settings->directory, S8("metal-reader"));
            DObservation air_compile = GPU_TOOLS_RUN(settings, S8("compile-air"), evidence->ide, S8("cc"), S8("--target=air64-apple-macos"),
                S8("--xcrun"), settings->xcrun, S8("-c"), source, S8("-o"), air);
            ok &= d_success(air_compile) && gpu_tools_hash(settings, air, (String8){0});
            String8 bad_air = path_join(arena, settings->directory, S8("invalid.air"));
            d_write(evidence, bad_air, S8("BC\xc0\xde\0\0\0\0"));
            DObservation air_reject = GPU_TOOLS_RUN(settings, S8("reject-air"), settings->xcrun, S8("-sdk"), S8("macosx"), S8("metallib"),
                bad_air, S8("-o"), path_join(arena, settings->directory, S8("rejected.metallib")));
            ok &= gpu_tools_rejected(air_reject) && gpu_tools_hash(settings, bad_air, (String8){0});
            // The real metallib linker consumes the AIR emitted through ide.
            compiled = GPU_TOOLS_RUN(settings, S8("compile"), evidence->ide, S8("cc"), S8("--target=air64-apple-macos"),
                S8("--xcrun"), settings->xcrun, air, S8("-o"), artifact);
            DObservation reader_build = GPU_TOOLS_RUN(settings, S8("build-reader"), settings->xcrun, S8("-sdk"), S8("macosx"), S8("clang"),
                gpu_tools_fixtures[4].path, S8("-framework"), S8("Metal"), S8("-framework"), S8("Foundation"), S8("-lobjc"), S8("-o"), reader);
            ok &= d_success(reader_build) && gpu_tools_hash(settings, reader, (String8){0});
            consumed = GPU_TOOLS_RUN(settings, S8("load-library"), reader, artifact);
            ok &= d_contains(consumed.output, S8("METAL_CONSUMER accepted buster_gpu_smoke"));
            malformed = S8("MTLB\0\0\0\0\0\0\0\0\0\0\0\0");
            d_write(evidence, invalid, malformed);
            rejected = GPU_TOOLS_RUN(settings, S8("reject"), reader, invalid);
        }
    }
    ok &= d_success(compiled) && d_success(consumed) && gpu_tools_rejected(rejected);
    if (d_success(compiled)) { ok &= gpu_tools_hash(settings, artifact, (String8){0}); }
    if (path_exists(arena, invalid)) { ok &= gpu_tools_hash(settings, invalid, (String8){0}); }
    return ok && !evidence->io_failed;
}

BUSTER_GLOBAL_LOCAL u32 gpu_tools_self_test(Arena* arena)
{
    DObservation result = {.kind = D_EXIT, .status = 1, .error = S8("invalid")};
    u32 failures = !gpu_tools_rejected(result);
    result.status = 0; failures += gpu_tools_rejected(result);
    result.status = 1; result.kind = D_SPAWN; failures += gpu_tools_rejected(result);
    result.kind = D_TIMEOUT; failures += gpu_tools_rejected(result);
    result.kind = D_SIGNAL; failures += gpu_tools_rejected(result);
    result.kind = D_EXIT; result.sanitizer = true; failures += gpu_tools_rejected(result);
    result.sanitizer = false; result.error = (String8){0}; failures += gpu_tools_rejected(result);
    TpHash hash;
    char digest[65];
    tp_hash_init(&hash);
    tp_hash_add(&hash, "abc", 3);
    tp_hash_finish(&hash, digest);
    failures += strcmp(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0;
    // The common evidence observer's self-test covers process errors, timeouts,
    // arbitrary argv bytes and refusing an already-owned output directory.
    failures += d_self_test(arena);
    string_print(S8("GPU_TOOLCHAINS_SELF_TEST failures={u32}\n"), failures);
    return failures;
}

BUSTER_GLOBAL_LOCAL ProcessResult gpu_tools_main(Arena* arena, SliceString8 arguments)
{
    GpuTools settings = {.evidence = {.arena = arena, .ide = S8("build/Release/ide"), .out = S8("build/gpu-toolchains"), .timeout_seconds = 60}};
    bool selected[GPU_TOOLS_PROFILE_COUNT] = {0};
    bool valid = true, self_test = false;
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        String8 option = arguments.pointer[index];
        if (string_equal(option, S8("--self-test"))) { self_test = true; }
        else if (index + 1 >= arguments.length) { valid = false; }
        else
        {
            String8 value = arguments.pointer[++index];
            if (string_equal(option, S8("--profile")))
            {
                bool found = false;
                for (u32 profile = 0; profile < GPU_TOOLS_PROFILE_COUNT; profile += 1)
                {
                    if (string_equal(value, gpu_tools_profiles[profile])) { valid &= !selected[profile]; selected[profile] = true; found = true; }
                }
                valid &= found;
            }
            else if (string_equal(option, S8("--ide"))) { settings.evidence.ide = value; }
            else if (string_equal(option, S8("--out"))) { settings.evidence.out = value; }
            else if (string_equal(option, S8("--timeout"))) { valid &= d_number(value, &settings.evidence.timeout_seconds) && settings.evidence.timeout_seconds > 0 && settings.evidence.timeout_seconds <= 3600; }
            else { valid = false; }
        }
    }
    if (self_test) { valid &= arguments.length == 1; }
    u32 failures = 0, passed = 0, skipped = 0;
    if (!valid) { string_print(S8("test_gpu_toolchains: invalid arguments; see docs/gpu-toolchain-validation.md\n")); failures += 1; }
    else if (self_test) { failures += gpu_tools_self_test(arena); }
    else if (!d_create_output(arena, settings.evidence.out))
    {
        string_print(S8("test_gpu_toolchains: --out must be a fresh directory\n")); failures += 1;
    }
    else
    {
        DSettings* evidence = &settings.evidence;
        settings.directory = evidence->out;
        String8 report = string_duplicate_arena(arena, path_join(arena, evidence->out, S8("commands.tsv")), true);
        evidence->report = fopen((char*)report.pointer, "wb");
        evidence->io_failed = !evidence->report;
        d_write(evidence, path_join(arena, evidence->out, S8("format.txt")),
            S8("version=1\ncommands.tsv: prefix kind exit_status raw_status sanitizer elapsed_us\nkind: 0=exit 1=signal 2=timeout 3=spawn 4=wait\n*.argv: NUL-delimited argv; *.stdout/*.stderr: exact captured bytes\nhash-*.txt: SHA-256 of fixtures, selected executables and produced artifacts\n"));
        bool inputs_ok = d_success(GPU_TOOLS_RUN(&settings, S8("revision"), S8("git"), S8("rev-parse"), S8("HEAD")));
        inputs_ok &= d_success(GPU_TOOLS_RUN(&settings, S8("worktree"), S8("git"), S8("status"), S8("--porcelain")));
        inputs_ok &= !evidence->io_failed;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(gpu_tools_fixtures); index += 1)
        { inputs_ok &= gpu_tools_hash(&settings, gpu_tools_fixtures[index].path, gpu_tools_fixtures[index].sha256); }
        settings.compiler = gpu_tools_path(arena, S8("BUSTER_GPU_CLANG"), S8("clang-18"));
        settings.llc = gpu_tools_path(arena, S8("BUSTER_GPU_LLC"), S8("llc-18"));
        settings.dxc = gpu_tools_path(arena, S8("BUSTER_DXC"), S8("dxc"));
        settings.dxv = gpu_tools_path(arena, S8("BUSTER_GPU_TEST_DXV"), S8("dxv"));
        settings.readobj = gpu_tools_path(arena, S8("BUSTER_GPU_TEST_READOBJ"), S8("llvm-readobj-18"));
        settings.objdump = gpu_tools_path(arena, S8("BUSTER_GPU_TEST_OBJDUMP"), S8("llvm-objdump-18"));
        settings.spirv_val = gpu_tools_path(arena, S8("BUSTER_GPU_TEST_SPIRV_VAL"), S8("spirv-val"));
        settings.ptxas = gpu_tools_path(arena, S8("BUSTER_GPU_TEST_PTXAS"), S8("ptxas"));
        settings.xcrun = gpu_tools_path(arena, S8("BUSTER_XCRUN"), S8("xcrun"));
        settings.xcodebuild = gpu_tools_path(arena, S8("BUSTER_GPU_TEST_XCODEBUILD"), S8("xcodebuild"));
        String8 summary = S8("version=1\n");
        bool ide_checked = false;
        for (u32 profile = 0; profile < GPU_TOOLS_PROFILE_COUNT; profile += 1)
        {
            bool ok = false;
            if (selected[profile])
            {
                if (!ide_checked) { inputs_ok &= gpu_tools_hash(&settings, evidence->ide, (String8){0}); ide_checked = true; }
                ok = inputs_ok && gpu_tools_profile(&settings, profile);
                passed += ok; failures += !ok;
            }
            else { skipped += 1; }
            String8 status = !selected[profile] ? S8("SKIP_NOT_CONFIGURED") : ok ? S8("PASS") : S8("FAIL");
            String8 line = string_format(arena, S8("GPU_PROFILE {S8} {S8}\n"), gpu_tools_profiles[profile], status);
            string_print(S8("{S8}"), line);
            summary = string_format(arena, S8("{S8}{S8}"), summary, line);
        }
        failures += !inputs_ok;
        if (evidence->report) { evidence->io_failed |= fclose(evidence->report) != 0; }
        summary = string_format(arena, S8("{S8}GPU_TOOLCHAINS_SUMMARY passed={u32} skipped={u32} failures={u32} io_failed={u32}\n"),
            summary, passed, skipped, failures, (u32)evidence->io_failed);
        d_write(evidence, path_join(arena, evidence->out, S8("summary.txt")), summary);
        failures += evidence->io_failed;
    }
    string_print(S8("GPU_TOOLCHAINS_SUMMARY passed={u32} skipped={u32} failures={u32}\n"), passed, skipped, failures);
    return failures ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
}
#undef GPU_TOOLS_RUN
