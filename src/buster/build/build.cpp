#pragma once

#define BUSTER_USE_PADDING 0

#include <buster/base.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/target.h>
#include <buster/entry_point.h>
#include <buster/assertion.h>
// #include <martins/md5.h>
#include <buster/system_headers.h>
#include <buster/path.h>
#include <buster/file.h>
#include <buster/build/build_common.h>
#include <buster/os.h>
#include <buster/arguments.h>

#include <buster/test.h>
#if BUSTER_UNITY_BUILD
#include <buster/os.cpp>
#include <buster/arena.cpp>
#include <buster/assertion.cpp>
#include <buster/target.cpp>
#include <buster/memory.cpp>
#include <buster/string.cpp>
#include <buster/integer.cpp>
#include <buster/file.cpp>
#include <buster/build/build_common.cpp>
#if defined(__x86_64__)
#include <buster/x86_64.cpp>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.cpp>
#endif
#include <buster/entry_point.cpp>
#include <buster/path.cpp>
#include <buster/file.cpp>
#include <buster/arguments.cpp>
#include <buster/test.cpp>
#endif

BUSTER_GLOBAL_LOCAL __attribute__((used)) StringOs toolchain_path = {};
BUSTER_GLOBAL_LOCAL StringOs clang_path = {};
BUSTER_GLOBAL_LOCAL __attribute__((used)) StringOs xc_sdk_path = {};
BUSTER_GLOBAL_LOCAL bool is_stderr_tty = true;

#define BUSTER_TODO() print(S8("TODO\n")); fail()

ENUM_T(ModuleId, u64,
    MODULE_BASE,
    MODULE_ARENA,
    MODULE_ASSERTION,
    MODULE_BUILD_COMMON,
    MODULE_BUILDER,
    MODULE_FILE,
    MODULE_FLOAT,
    MODULE_INTEGER,
    MODULE_MEMORY,
    MODULE_OS,
    MODULE_PATH,
    MODULE_STRING,
    MODULE_SYSTEM_HEADERS,
    MODULE_TEST,
    MODULE_TIME,
    MODULE_ENTRY_POINT,
    MODULE_TARGET,
    MODULE_X86_64,
    MODULE_AARCH64,
    MODULE_MD5,
    MODULE_CC_MAIN,
    MODULE_ASM_MAIN,
    MODULE_IR,
    MODULE_INSTRUCTION_SELECTION,
    MODULE_CODEGEN,
    MODULE_LINK,
    MODULE_LINK_JIT,
    MODULE_LINK_ELF,
    MODULE_UI_CORE,
    MODULE_RENDERING,
    MODULE_WINDOW,
    MODULE_IDE,
    MODULE_FONT_PROVIDER,
    MODULE_UI_BUILDER,
    MODULE_ARGUMENTS,
    MODULE_SCRAPE_XED,
    MODULE_SCRAPE_LLVM,
    MODULE_SIMD,
    MODULE_BUSTER_PARSER,
    MODULE_OPTIMIZING_IR);

ENUM(DirectoryId,
    DIRECTORY_SRC_BUSTER,
    DIRECTORY_SRC_MARTINS,
    DIRECTORY_ROOT,
    DIRECTORY_FRONTEND_CC,
    DIRECTORY_FRONTEND_ASM,
    DIRECTORY_IR,
    DIRECTORY_BACKEND,
    DIRECTORY_LINK,
    DIRECTORY_BUILD,
    DIRECTORY_IDE,
    DIRECTORY_FRONTEND_BUSTER);

STRUCT(Module)
{
    DirectoryId directory;
    bool no_header;
    bool no_source;
    u8 reserved[2];
};

STRUCT(TargetBuildFile)
{
    FileStats stats;
    StringOs full_path;
    BuildTarget* target;
    u64 has_debug_information:1;
    u64 use_io_ring:1;
    u64 use_graphics:1;
    u64 optimize:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 reserved:58;
};

STRUCT(ModuleInstantiation)
{
    BuildTarget* target;
    u64 index;
    ModuleId id;
};

STRUCT(LinkModule)
{
    u64 index;
    ModuleId id;
};

STRUCT(ModuleSlice)
{
    LinkModule* pointer;
    u64 length;
};

BUSTER_GLOBAL_LOCAL Module modules[] = {
    [(u64)ModuleId::MODULE_ARENA] = {},
    [(u64)ModuleId::MODULE_ASSERTION] = {},
    [(u64)ModuleId::MODULE_BUILD_COMMON] = {},
    [(u64)ModuleId::MODULE_FILE] = {},
    [(u64)ModuleId::MODULE_FLOAT] = {},
    [(u64)ModuleId::MODULE_INTEGER] = {},
    [(u64)ModuleId::MODULE_MEMORY] = {},
    [(u64)ModuleId::MODULE_OS] = {},
    [(u64)ModuleId::MODULE_PATH] = {},
    [(u64)ModuleId::MODULE_STRING] = {},
    [(u64)ModuleId::MODULE_TEST] = {},
    [(u64)ModuleId::MODULE_TIME] = {},
    [(u64)ModuleId::MODULE_ENTRY_POINT] = {},
    [(u64)ModuleId::MODULE_TARGET] = {},
    [(u64)ModuleId::MODULE_X86_64] = {},
    [(u64)ModuleId::MODULE_AARCH64] = {},
    [(u64)ModuleId::MODULE_BASE] = { .no_source = true },
    [(u64)ModuleId::MODULE_SYSTEM_HEADERS] = { .no_source = true, },
    [(u64)ModuleId::MODULE_BUILDER] = {
        .directory = DirectoryId::DIRECTORY_ROOT,
        .no_header = true,
    },
    [(u64)ModuleId::MODULE_MD5] = {
        .directory = DirectoryId::DIRECTORY_SRC_MARTINS,
        .no_source = true,
    },
    [(u64)ModuleId::MODULE_CC_MAIN] = {
        .directory = DirectoryId::DIRECTORY_FRONTEND_CC,
        .no_header = true,
    },
    [(u64)ModuleId::MODULE_ASM_MAIN] = {
        .directory = DirectoryId::DIRECTORY_FRONTEND_ASM,
        .no_header = true,
    },
    [(u64)ModuleId::MODULE_IR] = {
        .directory = DirectoryId::DIRECTORY_IR,
    },
    [(u64)ModuleId::MODULE_INSTRUCTION_SELECTION] = {
        .directory = DirectoryId::DIRECTORY_BACKEND,
    },
    [(u64)ModuleId::MODULE_CODEGEN] = {
        .directory = DirectoryId::DIRECTORY_BACKEND,
    },
    [(u64)ModuleId::MODULE_LINK] = {
        .directory = DirectoryId::DIRECTORY_LINK,
    },
    [(u64)ModuleId::MODULE_LINK_JIT] = {
        .directory = DirectoryId::DIRECTORY_LINK,
    },
    [(u64)ModuleId::MODULE_LINK_ELF] = {
        .directory = DirectoryId::DIRECTORY_LINK,
    },
    [(u64)ModuleId::MODULE_UI_CORE] = {},
    [(u64)ModuleId::MODULE_RENDERING] = {},
    [(u64)ModuleId::MODULE_WINDOW] = {},
    [(u64)ModuleId::MODULE_IDE] = {
        .directory = DirectoryId::DIRECTORY_IDE,
    },
    [(u64)ModuleId::MODULE_FONT_PROVIDER] = {},
    [(u64)ModuleId::MODULE_UI_BUILDER] = {},
    [(u64)ModuleId::MODULE_ARGUMENTS] = {},
    [(u64)ModuleId::MODULE_SCRAPE_XED] = { .no_header = true },
    [(u64)ModuleId::MODULE_SCRAPE_LLVM] = { .no_header = true },
    [(u64)ModuleId::MODULE_SIMD] = {},
    [(u64)ModuleId::MODULE_BUSTER_PARSER] = {
        .directory = DirectoryId::DIRECTORY_FRONTEND_BUSTER,
    },
    [(u64)ModuleId::MODULE_OPTIMIZING_IR] = {
        .directory = DirectoryId::DIRECTORY_IR,
    },
};

static_assert(BUSTER_ARRAY_LENGTH(modules) == (u64)ModuleId::Count);

// TODO: better naming convention
STRUCT(LinkUnitSpecification)
{
    StringOs name;
    ModuleSlice modules;
    StringOs artifact_path;
    BuildTarget* target;
    StringOsList link_arguments;
    StringOsList run_arguments;
    ProcessSpawnResult link_spawn;
    ProcessSpawnResult run_spawn;
    u64 use_io_ring:1;
    u64 use_graphics:1;
    u64 has_debug_information:1;
    u64 optimize:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 is_builder:1;
    u64 reserved:57;
};

#if defined(__x86_64__)
BUSTER_GLOBAL_LOCAL constexpr ModuleId cpu_native_module = ModuleId::MODULE_X86_64;
#elif defined(__aarch64__)
BUSTER_GLOBAL_LOCAL constexpr ModuleId cpu_native_module = ModuleId::MODULE_AARCH64;
#endif

// LinkModule builder_modules[] = {
//     { .id = MODULE_BUILDER },
//     { .id = MODULE_LIB },
//     { .id = MODULE_SYSTEM_HEADERS },
//     { .id = MODULE_ENTRY_POINT },
//     // { MODULE_MD5 },
//     { .id = native_module },
//     { .id = MODULE_TARGET }
// };

BUSTER_GLOBAL_LOCAL LinkModule __attribute__((unused)) cc_modules[] = {
    { .id = ModuleId::MODULE_CC_MAIN },
    { .id = ModuleId::MODULE_BASE },
    { .id = ModuleId::MODULE_OS },
    { .id = ModuleId::MODULE_ASSERTION },
    { .id = ModuleId::MODULE_ARENA },
    { .id = ModuleId::MODULE_INTEGER },
    { .id = ModuleId::MODULE_MEMORY },
    { .id = ModuleId::MODULE_SYSTEM_HEADERS },
    { .id = ModuleId::MODULE_ENTRY_POINT },
    { .id = ModuleId::MODULE_IR },
    { .id = ModuleId::MODULE_INSTRUCTION_SELECTION },
    { .id = ModuleId::MODULE_FILE },
    { .id = ModuleId::MODULE_PATH },
    { .id = ModuleId::MODULE_CODEGEN },
    { .id = ModuleId::MODULE_LINK },
    { .id = ModuleId::MODULE_LINK_JIT },
    { .id = ModuleId::MODULE_LINK_ELF },
    { .id = cpu_native_module },
    { .id = ModuleId::MODULE_TARGET },
    { .id = ModuleId::MODULE_STRING },
    { .id = ModuleId::MODULE_TEST },
    { .id = ModuleId::MODULE_ARGUMENTS },
    { .id = ModuleId::MODULE_SIMD },
};

BUSTER_GLOBAL_LOCAL LinkModule __attribute__((unused)) asm_modules[] = {
    { .id = ModuleId::MODULE_ASM_MAIN },
    { .id = ModuleId::MODULE_BASE },
    { .id = ModuleId::MODULE_ASSERTION },
    { .id = ModuleId::MODULE_OS },
    { .id = ModuleId::MODULE_ARENA },
    { .id = ModuleId::MODULE_FLOAT },
    { .id = ModuleId::MODULE_INTEGER },
    { .id = ModuleId::MODULE_MEMORY },
    { .id = ModuleId::MODULE_SYSTEM_HEADERS },
    { .id = ModuleId::MODULE_ENTRY_POINT },
    { .id = cpu_native_module },
    { .id = ModuleId::MODULE_TARGET },
    { .id = ModuleId::MODULE_STRING },
    { .id = ModuleId::MODULE_TEST },
    { .id = ModuleId::MODULE_ARGUMENTS },
};

BUSTER_GLOBAL_LOCAL LinkModule __attribute__((unused)) ide_modules[] = {
    { .id = ModuleId::MODULE_IDE },
    { .id = ModuleId::MODULE_BASE },
    { .id = ModuleId::MODULE_ASSERTION },
    { .id = ModuleId::MODULE_OS },
    { .id = ModuleId::MODULE_ARENA },
    { .id = ModuleId::MODULE_FLOAT },
    { .id = ModuleId::MODULE_INTEGER },
    { .id = ModuleId::MODULE_MEMORY },
    { .id = ModuleId::MODULE_SYSTEM_HEADERS },
    { .id = ModuleId::MODULE_ENTRY_POINT },
    { .id = cpu_native_module },
    { .id = ModuleId::MODULE_TARGET },
    { .id = ModuleId::MODULE_STRING },
    { .id = ModuleId::MODULE_WINDOW },
    { .id = ModuleId::MODULE_RENDERING },
    { .id = ModuleId::MODULE_UI_CORE },
    { .id = ModuleId::MODULE_TEST },
    { .id = ModuleId::MODULE_FILE },
    { .id = ModuleId::MODULE_FONT_PROVIDER },
    { .id = ModuleId::MODULE_UI_BUILDER },
    { .id = ModuleId::MODULE_TIME },
    { .id = ModuleId::MODULE_ARGUMENTS },
    { .id = ModuleId::MODULE_BUSTER_PARSER },
    { .id = ModuleId::MODULE_IR },
};

BUSTER_GLOBAL_LOCAL LinkModule __attribute__((unused)) scrape_xed_modules[] = {
    { .id = ModuleId::MODULE_SCRAPE_XED },
    { .id = ModuleId::MODULE_BASE },
    { .id = ModuleId::MODULE_OS },
    { .id = ModuleId::MODULE_ASSERTION },
    { .id = ModuleId::MODULE_ARENA },
    { .id = ModuleId::MODULE_INTEGER },
    { .id = ModuleId::MODULE_MEMORY },
    { .id = ModuleId::MODULE_SYSTEM_HEADERS },
    { .id = ModuleId::MODULE_ENTRY_POINT },
    { .id = cpu_native_module },
    { .id = ModuleId::MODULE_TARGET },
    { .id = ModuleId::MODULE_STRING },
    { .id = ModuleId::MODULE_FILE },
    { .id = ModuleId::MODULE_PATH },
    { .id = ModuleId::MODULE_TEST },
    { .id = ModuleId::MODULE_ARGUMENTS },
};

BUSTER_GLOBAL_LOCAL LinkModule __attribute__((unused)) scrape_llvm_modules[] = {
    { .id = ModuleId::MODULE_SCRAPE_LLVM },
    { .id = ModuleId::MODULE_BASE },
    { .id = ModuleId::MODULE_OS },
    { .id = ModuleId::MODULE_ASSERTION },
    { .id = ModuleId::MODULE_ARENA },
    { .id = ModuleId::MODULE_INTEGER },
    { .id = ModuleId::MODULE_MEMORY },
    { .id = ModuleId::MODULE_SYSTEM_HEADERS },
    { .id = ModuleId::MODULE_ENTRY_POINT },
    { .id = cpu_native_module },
    { .id = ModuleId::MODULE_TARGET },
    { .id = ModuleId::MODULE_STRING },
    { .id = ModuleId::MODULE_FILE },
    { .id = ModuleId::MODULE_PATH },
    { .id = ModuleId::MODULE_TEST },
    { .id = ModuleId::MODULE_ARGUMENTS },
};

BUSTER_GLOBAL_LOCAL u128 hash_file(u8* pointer, u64 length)
{
    BUSTER_CHECK(((u64)pointer & (64 - 1)) == 0);
    u128 digest = 0;
    if (length)
    {
        // TODO:
        // md5_ctx ctx;
        // md5_init(&ctx);
        // md5_update(&ctx, pointer, length);
        // static_assert(sizeof(digest) == MD5_DIGEST_SIZE);
        // md5_finish(&ctx, (u8*)&digest);
    }
    return digest;
}

ENUM(TaskId,
    TASK_ID_COMPILATION,
    TASK_ID_LINKING);

ENUM(ProjectId,
    PROJECT_OPERATING_SYSTEM_BUILDER,
    PROJECT_OPERATING_SYSTEM_BOOTLOADER,
    PROJECT_OPERATING_SYSTEM_KERNEL);

STRUCT(CompilationUnit)
{
    BuildTarget* target;
    StringOs compiler;
    StringOsList compilation_arguments;
    StringOs object_path;
    StringOs source_path;
    ProcessSpawnResult compile_spawn;
    // Process process;
    u64 optimize:1;
    u64 has_debug_information:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 use_io_ring:1;
    u64 use_graphics:1;
    u64 include_tests:1;
    u64 reserved:57;
#if BUSTER_USE_PADDING
    u8 cache_padding[BUSTER_MIN(BUSTER_CACHE_LINE_GUESS - ((5 * sizeof(u64))), BUSTER_CACHE_LINE_GUESS)];
#endif
};

#if BUSTER_USE_PADDING
static_assert(sizeof(CompilationUnit) % BUSTER_CACHE_LINE_GUESS == 0);
#endif

BUSTER_GLOBAL_LOCAL void append_string8(Arena* arena, String8 s)
{
    string8_duplicate_arena(arena, s, false);
}

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL void append_string16(Arena* arena, String16 s)
{
    string16_to_string8_arena(arena, s, false);
}

#define append_os_string append_string16
#else
#define append_os_string append_string8
#endif

BUSTER_GLOBAL_LOCAL bool target_equal(BuildTarget* a, BuildTarget* b)
{
    bool result = a == b;
    if (!result)
    {
        result = memcmp(a->pointer, b->pointer, sizeof(*a->pointer)) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult builder_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {};
    return result;
}

ENUM(BatchArena,
    BATCH_ARENA_GENERAL,
    BATCH_ARENA_MODULE_LIST,
    BATCH_ARENA_FILE_LIST,
    BATCH_ARENA_COMPILE_COMMANDS);

STRUCT(BatchTestConfiguration)
{
    Arena* arenas[(u64)BatchArena::Count];
    StringOs function_optimization_log;
    u64 fuzz_time_seconds;
    u64 optimize:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 has_debug_information:1;
    u64 unity_build:1;
    u64 just_preprocessor:1;
    u64 time_build:1;
    u64 include_tests:1;
    u64 reserved:56;
};

ENUM(ShaderStage,
    SHADER_STAGE_VERTEX,
    SHADER_STAGE_FRAGMENT);

STRUCT(ShaderCompilation)
{
    StringOs source_path;
    StringOs output_path;
    ProcessSpawnResult spawn;
    StringOsList arguments;
};

STRUCT(ShaderModule)
{
    StringOs name;
    ShaderCompilation compilations[(u64)ShaderStage::Count];
};

BUSTER_GLOBAL_LOCAL BatchTestResult single_run(const BatchTestConfiguration* const configuration)
{
    BatchTestResult result = {};
    let general_arena = configuration->arenas[(u64)BatchArena::BATCH_ARENA_GENERAL];
    let cwd = path_absolute_arena(general_arena, SOs("."));

    LinkUnitSpecification specifications[] = {
        { .name = SOs("ide"), .modules = (ModuleSlice) BUSTER_ARRAY_TO_SLICE(ide_modules), },
        // { .name = SOs("cc"), .modules = (ModuleSlice) BUSTER_ARRAY_TO_SLICE(cc_modules), },
        // { .name = SOs("asm"), .modules = (ModuleSlice) BUSTER_ARRAY_TO_SLICE(asm_modules), },
        { .name = SOs("scrape_xed"), .modules = (ModuleSlice) BUSTER_ARRAY_TO_SLICE(scrape_xed_modules), },
        // { .name = SOs("scrape_llvm"), .modules = (ModuleSlice) BUSTER_ARRAY_TO_SLICE(scrape_llvm_modules), },
    };
    constexpr u64 link_unit_count = BUSTER_ARRAY_LENGTH(specifications);
    u64 default_target = 0;

    BuildTarget* target_buffer[link_unit_count];
    u64 target_count = 0;

    ShaderModule shader_modules[] = {
        { .name = SOs("rect"), },
    };
    let build_directory = SOs("build");
    let shader_source_directory_path = SOs("src/buster/shaders");
    StringOs include_parts[] = {
        SOs("-I"),
        shader_source_directory_path,
    };
    let include_flag = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(include_parts), true);
    let shader_output_directory_path = build_directory;

    StringOs stage_extensions[] = {
        [(u64)ShaderStage::SHADER_STAGE_VERTEX] = SOs(".vert"),
        [(u64)ShaderStage::SHADER_STAGE_FRAGMENT] = SOs(".frag"),
    };
    static_assert(BUSTER_ARRAY_LENGTH(stage_extensions) == (u64)ShaderStage::Count);
    bool capture = true;

    for (u64 shader_i = 0; shader_i < BUSTER_ARRAY_LENGTH(shader_modules); shader_i += 1)
    {
        let shader_module = &shader_modules[shader_i];

        for (EACH_ENUM_INT(ShaderStage, stage))
        {
            let compilation = &shader_module->compilations[stage];

            let extension = stage_extensions[stage];
            {
                StringOs parts[] = {
                    shader_source_directory_path,
                    SOs("/"),
                    shader_module->name,
                    extension,
                };

                compilation->source_path = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(parts), true);
            }

            {
                StringOs parts[] = {
                    shader_output_directory_path,
                    SOs("/"),
                    shader_module->name,
                    extension,
                    SOs(".spv"),
                };

                compilation->output_path = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(parts), true);
            }

            let shader_compiler = SOs("glslangValidator");
            StringOs shader_compilation_arguments[] = {
                shader_compiler,
                SOs("-V"),
                compilation->source_path,
                SOs("-o"),
                compilation->output_path,
                include_flag,
            };
            let arguments = string_os_list_create_from(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(shader_compilation_arguments));
            compilation->spawn = os_process_spawn(shader_compilation_arguments[0], arguments, program_state->input.envp, (ProcessSpawnOptions){ .capture = capture ? ((u64)1 << (u64)StandardStream::Output) : 0 });
            compilation->arguments = arguments;
        }
    }

    for (u64 i = 0; i < link_unit_count; i += 1)
    {
        let link_unit = &specifications[i];
        let target = &build_target_native;
        if (configuration->unity_build)
        {
            link_unit->modules.length = 1;
        }
        link_unit->target = target;
        link_unit->optimize = configuration->optimize;
        link_unit->has_debug_information = configuration->has_debug_information;
        link_unit->fuzz = configuration->fuzz;
        link_unit->sanitize = configuration->sanitize;
        link_unit->is_builder = string_equal(link_unit->name, SOs("builder"));
        link_unit->use_graphics = string_equal(link_unit->name, SOs("ide"));

        u64 target_i;
        for (target_i = 0; target_i < target_count; target_i += 1)
        {
            let target_candidate = target_buffer[target_i];
            if (target == target_candidate)
            {
                break;
            }
        }

        if (target_i == target_count)
        {
            target_buffer[target_i] = target;

            let march = target->pointer->cpu_arch == CpuArch::CPU_ARCH_X86_64 ? SOs("-march=") : SOs("-mcpu=");
            let target_strings = target_to_split_string_os(*target->pointer);
            StringOs march_parts[] = {
                march,
                target_strings.s[(u64)TargetStringComponents::TARGET_CPU_MODEL],
            };
            target->march_string = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(march_parts), true);

            StringOs triple_parts[2 * (u64)TargetStringComponents::Count - 1];
            for (EACH_ENUM_INT(TargetStringComponents, triple_i))
            {
                triple_parts[triple_i * 2 + 0] = target_strings.s[triple_i];
                if (triple_i < ((u64)TargetStringComponents::Count - 1))
                {
                    triple_parts[triple_i * 2 + 1] = SOs("-");
                }
            }

            let target_triple = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(triple_parts), true);
            target->string = target_triple;

            StringOs directory_path_parts[] = {
                cwd,
                SOs("/build/"),
                target_triple,
            };
            let directory = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(directory_path_parts), true);
            target->directory_path = directory;
            os_make_directory(directory);
            target_count += 1;
        }

        StringOs artifact_path_parts[] = {
            cwd,
            SOs("/build/"),
            link_unit->is_builder ? SOs("") : target->string,
            link_unit->is_builder ? SOs("") : SOs("/"),
            link_unit->name,
            target->pointer->os == OperatingSystem::OPERATING_SYSTEM_WINDOWS ? SOs(".exe") : SOs(""),
        };

        let artifact_path = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(artifact_path_parts), true);
        link_unit->artifact_path = artifact_path;
    }

    StringOs directory_paths[] = {
        [(u64)DirectoryId::DIRECTORY_SRC_BUSTER] = SOs("src/buster"),
        [(u64)DirectoryId::DIRECTORY_SRC_MARTINS] = SOs("src/martins"),
        [(u64)DirectoryId::DIRECTORY_ROOT] = SOs(""),
        [(u64)DirectoryId::DIRECTORY_FRONTEND_CC] = SOs("src/buster/compiler/frontend/cc"),
        [(u64)DirectoryId::DIRECTORY_FRONTEND_ASM] = SOs("src/buster/compiler/frontend/asm"),
        [(u64)DirectoryId::DIRECTORY_IR] = SOs("src/buster/compiler/ir"),
        [(u64)DirectoryId::DIRECTORY_BACKEND] = SOs("src/buster/compiler/backend"),
        [(u64)DirectoryId::DIRECTORY_LINK] = SOs("src/buster/compiler/link"),
        [(u64)DirectoryId::DIRECTORY_BUILD] = SOs("src/buster/build"),
        [(u64)DirectoryId::DIRECTORY_IDE] = SOs("src/buster/ide"),
        [(u64)DirectoryId::DIRECTORY_FRONTEND_BUSTER] = SOs("src/buster/compiler/frontend/buster"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(directory_paths) == (u64)DirectoryId::Count);

    StringOs module_names[] = {
        [(u64)ModuleId::MODULE_BASE] = SOs("base"),
        [(u64)ModuleId::MODULE_SYSTEM_HEADERS] = SOs("system_headers"),
        [(u64)ModuleId::MODULE_ENTRY_POINT] = SOs("entry_point"),
        [(u64)ModuleId::MODULE_TARGET] = SOs("target"),
        [(u64)ModuleId::MODULE_X86_64] = SOs("x86_64"),
        [(u64)ModuleId::MODULE_AARCH64] = SOs("aarch64"),
        [(u64)ModuleId::MODULE_BUILDER] = SOs("build"),
        [(u64)ModuleId::MODULE_MD5] = SOs("md5"),
        [(u64)ModuleId::MODULE_MEMORY] = SOs("memory"),
        [(u64)ModuleId::MODULE_CC_MAIN] = SOs("cc_main"),
        [(u64)ModuleId::MODULE_ASM_MAIN] = SOs("asm_main"),
        [(u64)ModuleId::MODULE_IR] = SOs("ir"),
        [(u64)ModuleId::MODULE_INSTRUCTION_SELECTION] = SOs("instruction_selection"),
        [(u64)ModuleId::MODULE_CODEGEN] = SOs("code_generation"),
        [(u64)ModuleId::MODULE_LINK] = SOs("link"),
        [(u64)ModuleId::MODULE_LINK_ELF] = SOs("elf"),
        [(u64)ModuleId::MODULE_LINK_JIT] = SOs("jit"),
        [(u64)ModuleId::MODULE_ARENA] = SOs("arena"),
        [(u64)ModuleId::MODULE_ASSERTION] = SOs("assertion"),
        [(u64)ModuleId::MODULE_BUILD_COMMON] = SOs("build_common"),
        [(u64)ModuleId::MODULE_FILE] = SOs("file"),
        [(u64)ModuleId::MODULE_FLOAT] = SOs("float"),
        [(u64)ModuleId::MODULE_INTEGER] = SOs("integer"),
        [(u64)ModuleId::MODULE_OS] = SOs("os"),
        [(u64)ModuleId::MODULE_PATH] = SOs("path"),
        [(u64)ModuleId::MODULE_STRING] = SOs("string"),
        [(u64)ModuleId::MODULE_TEST] = SOs("test"),
        [(u64)ModuleId::MODULE_TIME] = SOs("time"),
        [(u64)ModuleId::MODULE_UI_CORE] = SOs("ui_core"),
        [(u64)ModuleId::MODULE_RENDERING] = SOs("rendering"),
        [(u64)ModuleId::MODULE_WINDOW] = SOs("window"),
        [(u64)ModuleId::MODULE_IDE] = SOs("ide"),
        [(u64)ModuleId::MODULE_FONT_PROVIDER] = SOs("font_provider"),
        [(u64)ModuleId::MODULE_UI_BUILDER] = SOs("ui_builder"),
        [(u64)ModuleId::MODULE_ARGUMENTS] = SOs("arguments"),
        [(u64)ModuleId::MODULE_SCRAPE_XED] = SOs("scrape_xed"),
        [(u64)ModuleId::MODULE_SCRAPE_LLVM] = SOs("scrape_llvm"),
        [(u64)ModuleId::MODULE_SIMD] = SOs("simd"),
        [(u64)ModuleId::MODULE_BUSTER_PARSER] = SOs("parser"),
        [(u64)ModuleId::MODULE_OPTIMIZING_IR] = SOs("optimizing_ir"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(module_names) == (u64)ModuleId::Count);

    if (!configuration->unity_build)
    {
        let cache_manifest = os_file_open(SOs("build/cache_manifest"), (OpenFlags) { .read = 1 }, (OpenPermissions){});
        if (cache_manifest)
        {
            let cache_manifest_stats = os_file_get_stats(cache_manifest, (FileStatsOptions){ .size = 1, .modified_time = 1 });
            let cache_manifest_buffer = (u8*)arena_allocate_bytes(general_arena, cache_manifest_stats.size, 64);
            os_file_read(cache_manifest, (ByteSlice){ cache_manifest_buffer, cache_manifest_stats.size }, cache_manifest_stats.size);
            os_file_close(cache_manifest);
            let cache_manifest_hash = hash_file(cache_manifest_buffer, cache_manifest_stats.size);
            BUSTER_UNUSED(cache_manifest_hash);
            string8_print(S8("TODO: Cache manifest found!\n"));
            os_fail();
        }
        else
        {
            BUSTER_UNUSED(target_native);
        }
    }

    let file_list_arena = configuration->arenas[(u64)BatchArena::BATCH_ARENA_FILE_LIST];
    let file_list_start = file_list_arena->position;
    let file_list = (TargetBuildFile*)((u8*)file_list_arena + file_list_start);
    u64 file_list_count = 0;

    let module_list_arena = configuration->arenas[(u64)BatchArena::BATCH_ARENA_MODULE_LIST];
    let module_list_start = module_list_arena->position;
    let module_list = (ModuleInstantiation*)((u8*)module_list_arena + module_list_start);
    u64 module_list_count = 0;

    u64 c_source_file_count = 0;

    let header_extension = SOs(".h");
    let source_extension = SOs(".cpp");

    for (u64 link_unit_index = 0; link_unit_index < link_unit_count; link_unit_index += 1)
    {
        let link_unit = &specifications[link_unit_index];
        let link_unit_modules = link_unit->modules;
        let link_unit_target = link_unit->target;

        for (u64 module_index = 0; module_index < link_unit_modules.length; module_index += 1)
        {
            let module = &link_unit_modules.pointer[module_index];
            let module_specification = modules[(u64)module->id];

            u64 i;
            for (i = 0; i < module_list_count; i += 1)
            {
                let existing_module = &module_list[i];
                if ((existing_module->id == module->id) & target_equal(existing_module->target, link_unit_target))
                {
                    break;
                }
            }

            if (i == module_list_count)
            {
                let count = (u64)1 + (!module_specification.no_header & !module_specification.no_source);
                file_list_count += count;
                let new_file = arena_allocate(file_list_arena, TargetBuildFile, count);
                let module_name = module_names[(u64)module->id];

                // This is wasteful, but it might not matter?
                StringOs parts[] = {
                    cwd,
                    SOs("/"),
                    directory_paths[(u64)module_specification.directory],
                    module_specification.directory == DirectoryId::DIRECTORY_ROOT ? SOs("") : SOs("/"),
                    module_name,
                    module_specification.no_source ? header_extension : source_extension,
                };
                let c_full_path = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(parts), true);

                *new_file = (TargetBuildFile) {
                    .full_path = c_full_path,
                    .target = link_unit_target,
                    .has_debug_information = link_unit->has_debug_information,
                    .optimize = link_unit->optimize,
                    .fuzz = link_unit->fuzz,
                    .sanitize = link_unit->sanitize,
                    .use_io_ring = link_unit->use_io_ring,
                    .use_graphics = link_unit->use_graphics,
                };

                let c_source_file_index = c_source_file_count;
                module->index = c_source_file_index;
                c_source_file_count = c_source_file_index + !module_specification.no_source;

                module_list[module_list_count++] = (ModuleInstantiation) {
                    .target = link_unit_target,
                    .id = module->id,
                    .index = module->index,
                };

                if (!module_specification.no_source & !module_specification.no_header)
                {
                    let h_full_path = string_os_duplicate_arena(general_arena, c_full_path, true);
                    h_full_path.pointer[h_full_path.length - 1] = 'h';
                    *(new_file + 1) = (TargetBuildFile) {
                        .full_path = h_full_path,
                        .target = link_unit_target,
                        .has_debug_information = link_unit->has_debug_information,
                        .optimize = link_unit->optimize,
                        .fuzz = link_unit->fuzz,
                        .sanitize = link_unit->sanitize,
                        .use_io_ring = link_unit->use_io_ring,
                        .use_graphics = link_unit->use_graphics,
                    };
                }
            }
            else
            {
                module->index = module_list[i].index;
            }
        }
    }

    let compilation_unit_count = c_source_file_count;
    let compilation_units = arena_allocate(general_arena, CompilationUnit, c_source_file_count);

    for (u64 file_i = 0, compilation_unit_i = 0; file_i < file_list_count; file_i += 1)
    {
        TargetBuildFile* source_file = &file_list[file_i]; 
        if (!configuration->unity_build)
        {
            let fd = os_file_open(BUSTER_SLICE_START(source_file->full_path, cwd.length + 1), (OpenFlags){ .read = 1 }, (OpenPermissions){});
            let stats = os_file_get_stats(fd, (FileStatsOptions){ .raw = UINT64_MAX });
            let buffer = (u8*)arena_allocate_bytes(general_arena, stats.size, 64);
            ByteSlice buffer_slice = { buffer, stats.size};
            os_file_read(fd, buffer_slice, stats.size);
            os_file_close(fd);
            let __attribute__((unused)) hash = hash_file(buffer_slice.pointer, buffer_slice.length);
        }

        if (string_os_ends_with_sequence(source_file->full_path, source_extension))
        {
            let compilation_unit = &compilation_units[compilation_unit_i];
            compilation_unit_i += 1;
            *compilation_unit = (CompilationUnit) {
                .target = source_file->target,
                .has_debug_information = source_file->has_debug_information,
                .use_io_ring = source_file->use_io_ring,
                .use_graphics = source_file->use_graphics,
                .source_path = source_file->full_path,
                .optimize = source_file->optimize,
                .fuzz = source_file->fuzz,
                .sanitize = source_file->sanitize,
            };
        }
    }

    for (u64 unit_i = 0; unit_i < compilation_unit_count; unit_i += 1)
    {
        let unit = &compilation_units[unit_i];

        let source_absolute_path = unit->source_path;
        let source_relative_path = BUSTER_SLICE_START(source_absolute_path, cwd.length + 1);
        let target_directory_path = unit->target->directory_path;
        StringOs object_absolute_path_parts[] = {
            target_directory_path,
            SOs("/"),
            source_relative_path,
#if defined(_WIN32)
            SOs(".obj"),
#else
            SOs(".o"),
#endif
        };

        let object_path = string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(object_absolute_path_parts), true);
        unit->object_path = object_path;

        CharOs buffer[BUSTER_MAX_PATH_LENGTH];
        let os_char_size = sizeof(buffer[0]);
        let copy_character_count = target_directory_path.length;
        memcpy(buffer, target_directory_path.pointer, (copy_character_count + 1) * os_char_size);
        buffer[copy_character_count] = 0;

        u64 buffer_i = copy_character_count;
        let buffer_start = buffer_i;
        u64 source_i = 0;
        while (1)
        {
            let source_remaining = BUSTER_SLICE_START(source_relative_path, source_i);
            let slash_index = string_os_first_code_unit(source_remaining, '/');
            if (slash_index == BUSTER_STRING_NO_MATCH)
            {
                break;
            }

            StringOs source_chunk = { source_remaining.pointer, slash_index };

            buffer[buffer_start + source_i] = '/';
            source_i += 1;

            let dst = buffer + buffer_start + source_i;
            let src = source_chunk;
            let byte_count = slash_index;

            memcpy(dst, src.pointer, byte_count * sizeof(src.pointer));
            let length = buffer_start + source_i + byte_count;
            buffer[length] = 0;

            let directory = string_os_from_pointer_length(buffer, length);
            os_make_directory(directory);

            source_i += byte_count; 
        }
    }

    if (!configuration->unity_build)
    {
        let compile_commands = configuration->arenas[(u64)BatchArena::BATCH_ARENA_COMPILE_COMMANDS];
        let compile_commands_start = compile_commands->position;
        append_string8(compile_commands, S8("[\n"));

        for (u64 unit_i = 0; unit_i < compilation_unit_count; unit_i += 1)
        {
            let unit = &compilation_units[unit_i];
            let source_absolute_path = unit->source_path;
            let object_path = unit->object_path;

            CompileLinkOptions options = {
                .function_optimization_log = configuration->function_optimization_log,
                .clang_path = clang_path,
                .xc_sdk_path = xc_sdk_path,
                .destination_path = object_path,
                .source_paths = &unit->source_path,
                .source_count = 1,
                .target = unit->target,
                .optimize = unit->optimize,
                .fuzz = unit->fuzz,
                .sanitize = unit->sanitize,
                .has_debug_information = unit->has_debug_information,
                .unity_build = configuration->unity_build,
                .use_io_ring = unit->use_io_ring,
                .use_graphics = unit->use_graphics,
                .just_preprocessor = configuration->just_preprocessor,
                .time_build = configuration->time_build,
                .include_tests = configuration->include_tests,
                .force_color = is_stderr_tty,
                .compile = 1,
                .link = configuration->unity_build,
            };
            let args = build_compile_link_arguments(general_arena, &options);

            unit->compiler = clang_path;
            unit->compilation_arguments = args;

            append_string8(compile_commands, S8("\t{\n\t\t\"directory\": \""));
            append_string8(compile_commands, string_os_to_string8_arena(general_arena, cwd));
            append_string8(compile_commands, S8("\",\n\t\t\"command\": \""));

            let arg_it = string_os_list_iterator_initialize(args);
            for (let arg = string_os_list_iterator_next(&arg_it); arg.pointer; arg = string_os_list_iterator_next(&arg_it))
            {
                let a = arg;
#ifndef _WIN32
                let double_quote_count = string8_code_point_count(a, '"');
                if (double_quote_count != 0)
                {
                    let new_length = a.length + double_quote_count;
                    a = (String8) { .pointer = arena_allocate(compile_commands, char8, new_length), .length = new_length };
                    bool is_double_quote;
                    for (u64 i = 0, double_quote_i = 0; i < arg.length; i += 1)
                    {
                        let original_ch = arg.pointer[i];
                        is_double_quote = original_ch == '"';
                        let escape_i = double_quote_i;
                        let character_i = double_quote_i + is_double_quote;
                        a.pointer[escape_i] = '\\';
                        a.pointer[character_i] = original_ch;
                        double_quote_i = character_i + 1;
                    }
                }
                else
                {
                    append_os_string(compile_commands, a);
                }
#else
                append_os_string(compile_commands, a);
#endif
                append_string8(compile_commands, S8(" "));
            }

            compile_commands->position -= 1;
            append_string8(compile_commands, S8("\",\n\t\t\"file\": \""));
            append_string8(compile_commands, string_os_to_string8_arena(general_arena, source_absolute_path));
            append_string8(compile_commands, S8("\"\n"));
            append_string8(compile_commands, S8("\t},\n"));
        }

        compile_commands->position -= 2;

        append_string8(compile_commands, S8("\n]"));

        let compile_commands_str = (String8){ .pointer = (char8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };
        result.process = file_write(SOs("build/compile_commands.json"), BUSTER_SLICE_TO_BYTE_SLICE(compile_commands_str)) ? ProcessResult::Success : ProcessResult::Failed;
    }

    for (u64 shader_i = 0; shader_i < BUSTER_ARRAY_LENGTH(shader_modules); shader_i += 1)
    {
        let shader_module = &shader_modules[shader_i];

        for (EACH_ENUM_INT(ShaderStage, stage))
        {
            let compilation = &shader_module->compilations[stage];
            let wait_result = os_process_wait_sync(general_arena, compilation->spawn);
            if (wait_result.result != ProcessResult::Success)
            {
                string8_print(S8("Shader compilation failed:\n{SOsL}\n"), compilation->arguments);
                result.process = wait_result.result;
            }
        }
    }

    if (result.process == ProcessResult::Success)
    {
        let selected_compilation_count = compilation_unit_count;
        let selected_compilation_units = compilation_units;

        if (!configuration->unity_build)
        {
            for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
            {
                let unit = &selected_compilation_units[unit_i];
                unit->compile_spawn = os_process_spawn(unit->compiler, unit->compilation_arguments, program_state->input.envp, (ProcessSpawnOptions){ .capture = capture ? ((u64)1 << (u64)StandardStream::Output) | ((u64)1 << (u64)StandardStream::Error) : 0 });
            }

            for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
            {
                let unit = &selected_compilation_units[unit_i];
                let unit_compilation_result = os_process_wait_sync(general_arena, unit->compile_spawn);

                for (EACH_ENUM_INT(StandardStream, stream))
                {
                    let standard_stream = unit_compilation_result.streams[stream];
                    if (standard_stream.length)
                    {
                        let stream_string = string8_from_pointer_length((char8*)standard_stream.pointer, standard_stream.length);
                        string8_print(S8("{S8}"), stream_string);
                    }
                }

                if (unit_compilation_result.result != ProcessResult::Success)
                {
                    result.process = unit_compilation_result.result;

                    if (flag_get(program_state->input.flags, ProgramFlag::Verbose))
                    {
                        string8_print(S8("FAILED to run the following compiling process: {SOsL}\n"), unit->compilation_arguments);
                    }
                }
            }
        }

        // TODO: depend more-fine grainedly, ie: link those objects which succeeded compiling instead of all or nothing
        if (result.process == ProcessResult::Success)
        {
            for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let link_unit_specification = &specifications[link_unit_i];
                let link_modules = link_unit_specification->modules;

                let source_paths = (StringOs*)align_forward((u64)((u8*)general_arena + general_arena->position), alignof(StringOs));
                u64 source_path_count = 0;

                u64 module_bitflag = 0;
                static_assert(sizeof(module_bitflag) * 8 >= (u64)ModuleId::Count);

                for (u64 module_i = 0; module_i < link_modules.length; module_i += 1)
                {
                    let module = &link_modules.pointer[module_i];

                    if ((module_bitflag & ((u64)1 << (u64)module->id)) == 0 && !modules[(u64)module->id].no_source)
                    {
                        let unit = &compilation_units[module->index];
                        let object_path = arena_allocate(general_arena, StringOs, 1);
                        *object_path = (configuration->just_preprocessor | configuration->unity_build) ? unit->source_path : unit->object_path;
                        source_path_count += 1;
                        module_bitflag |= (u64)1 << (u64)module->id;
                    }
                }

                CompileLinkOptions options = {
                    .function_optimization_log = configuration->function_optimization_log,
                    .clang_path = clang_path,
                    .xc_sdk_path = xc_sdk_path,
                    .destination_path = link_unit_specification->artifact_path,
                    .source_paths = source_paths,
                    .source_count = source_path_count,
                    .target = link_unit_specification->target,
                    .optimize = link_unit_specification->optimize,
                    .fuzz = link_unit_specification->fuzz,
                    .has_debug_information = link_unit_specification->has_debug_information,
                    .sanitize = link_unit_specification->sanitize,
                    .unity_build = configuration->unity_build,
                    .use_io_ring = link_unit_specification->use_io_ring,
                    .use_graphics = link_unit_specification->use_graphics,
                    .just_preprocessor = configuration->just_preprocessor,
                    .time_build = configuration->time_build,
                    .include_tests = configuration->include_tests,
                    .force_color = is_stderr_tty,
                    .compile = configuration->unity_build,
                    .link = 1,
                };
                let link_arguments = build_compile_link_arguments(general_arena, &options);
                link_unit_specification->link_arguments = link_arguments;
                link_unit_specification->link_spawn = os_process_spawn(clang_path, link_arguments, program_state->input.envp, (ProcessSpawnOptions){ .capture = capture ? ((u64)1 << (u64)StandardStream::Output) | ((u64)1 << (u64)StandardStream::Error) : 0 });
            }

            for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let link_unit = &specifications[link_unit_i];
                let link_result = os_process_wait_sync(general_arena, link_unit->link_spawn);

                for (EACH_ENUM_INT(StandardStream, stream))
                {
                    let standard_stream = link_result.streams[stream];
                    if (standard_stream.length)
                    {
                        let stream_string = string8_from_pointer_length((char8*)standard_stream.pointer, standard_stream.length);
                        string8_print(S8("{S8}"), stream_string);
                    }
                }

                if (link_result.result != ProcessResult::Success)
                {
                    result.process = link_result.result;

                    if (flag_get(program_state->input.flags, ProgramFlag::Verbose))
                    {
                        string8_print(S8("FAILED to run the following linking process: {SOsL}\n"), link_unit->link_arguments);
                    }
                }
            }
        }

        if (!configuration->just_preprocessor && result.process == ProcessResult::Success)
        {
            switch (build_program_state.command)
            {
                break; case BuildCommand::Count: BUSTER_UNREACHABLE();
                break; case BuildCommand::BUILD_COMMAND_BUILD: {}
                // TODO: fill
                break; case BuildCommand::BUILD_COMMAND_TEST_ALL: case BuildCommand::BUILD_COMMAND_TEST:
                {
                    UnitTestArguments arguments = {
                        .arena = general_arena,
                    };

                    let builder_unit_tests = builder_tests(&arguments);
                    consume_unit_tests(&result, builder_unit_tests);
                    if (!unit_test_succeeded(builder_unit_tests))
                    {
                        string8_print(S8("Build tests failed!\n"));
                    }

                    u64 fuzz_max_length = 4096;
                    let length_argument = string_os_format(general_arena, SOs("-max_len={u64}"), fuzz_max_length);
                    let fuzz_time_seconds = configuration->fuzz_time_seconds;
                    // Override momentarily since the fuzzing is a no-op for now
                    fuzz_time_seconds = 1;
                    let max_total_time_argument = string_os_format(general_arena, SOs("-max_total_time={u64}"), fuzz_time_seconds);

                    // Skip builder executable since we execute the tests ourselves
                    for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let link_unit_specification = &specifications[link_unit_i];

                        bool debug = false;

                        let program = link_unit_specification->artifact_path;

                        StringOs fuzz_arguments[] = {
                            program,
                            length_argument,
                            max_total_time_argument,
                        };

                        StringOs test_persist_parts[] = {
                            SOs("--test-persist="),
                            flag_get(program_state->input.flags, ProgramFlag::Test_Persist) && link_unit_i == default_target ? SOs("1") : SOs("0"),
                        };

                        StringOs test_arguments[] = {
                            program,
                            SOs("test"),
                            string_os_join_arena(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(test_persist_parts), true),
                        };

                        StringOs test_debug_arguments[] = {
                            SOs("/usr/bin/gdb"),
                            SOs("-ex"), SOs("set debuginfod enabled on"),
                            SOs("-ex"), SOs("set debuginfod urls https://debuginfod.ubuntu.com"),
                            SOs("-ex"), SOs("show debuginfod urls"),
                            SOs("-ex"), SOs("r"),
                            SOs("-ex"), SOs("up"),
                            SOs("-ex"), SOs("up"),
                            SOs("-ex"), SOs("bt"),
                            SOs("-ex"), SOs("p *rendering_handle"),
                            SOs("-ex"), SOs("p create_info"),
                            SOs("-ex"), SOs("p i"),
                            SOs("-ex"), SOs("p shader_modules[i]"),
                            SOs("--args"),
                            program,
                            SOs("test"),
                        };

                        let os_argument_slice = link_unit_specification->fuzz ? (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(fuzz_arguments) : debug ? (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(test_debug_arguments) : (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(test_arguments);
                        let os_arguments = string_os_list_create_from(general_arena, os_argument_slice);
                        link_unit_specification->run_arguments = os_arguments;
                        link_unit_specification->run_spawn = os_process_spawn(os_argument_slice.pointer[0], os_arguments, program_state->input.envp, (ProcessSpawnOptions){ .capture = capture && link_unit_i != default_target ? ((u64)1 << (u64)StandardStream::Output) | ((u64)1 << (u64)StandardStream::Error) : 0 });
                    }

                    for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let link_unit = &specifications[link_unit_i];
                        let test_result = os_process_wait_sync(general_arena, link_unit->run_spawn);

                        for (EACH_ENUM_INT(StandardStream, stream))
                        {
                            let standard_stream = test_result.streams[stream];
                            if (standard_stream.length)
                            {
                                let stream_string = string8_from_pointer_length((char8*)standard_stream.pointer, standard_stream.length);
                                string8_print(stream_string);
                            }
                        }

                        consume_external_tests(&result, test_result.result);
                        if (test_result.result != ProcessResult::Success)
                        {
                            let specification = &specifications[link_unit_i];
                            if (flag_get(program_state->input.flags, ProgramFlag::Verbose))
                            {
                                string8_print(S8("FAILED to run the following executable: {SOsL}\n"), specification->run_arguments);
                            }
                        }
                    }
                }
                break; case BuildCommand::BUILD_COMMAND_DEBUG:
                {
                    let default_unit = &specifications[default_target];
                    StringOs argument_descriptions[] = {
                        SOs("gf2"),
                        default_unit->artifact_path,
                    };
                    let arguments = string_os_list_create_from(general_arena, (Slice<StringOs>) BUSTER_ARRAY_TO_SLICE(argument_descriptions));
                    let spawn_result = os_process_spawn(argument_descriptions[0], arguments, program_state->input.envp, (ProcessSpawnOptions){});
                    if (spawn_result.handle)
                    {
                        let wait_result = os_process_wait_sync(0, spawn_result);
                        result.process = wait_result.result;
                    }
                    else
                    {
                        result.process = ProcessResult::Failed;
                    }
                }
            }
        }
    }
    else
    {
        string8_print(S8("Error writing compile commands: {EOs}"), os_get_last_error());
    }

    return result;
}

BUSTER_F_IMPL void async_user_tick()
{
}

BUSTER_F_IMPL ProcessResult entry_point()
{
    vulkan_sdk_path = os_get_environment_variable(SOs("VULKAN_SDK"));
    Arena* arenas[(u64)BatchArena::Count];
    for (EACH_ENUM_INT(BatchArena, i))
    {
        arenas[i] = arena_create((ArenaCreation){});
    }

    xc_sdk_path = build_program_state.string.values[(u64)BuildStringOption::BUILD_OPTION_STRING_XC_SDK_PATH];
    let toolchain_information = toolchain_get_information(arenas[(u64)BatchArena::BATCH_ARENA_GENERAL], current_llvm_version);
    clang_path = toolchain_information.clang_path;
    toolchain_path = toolchain_information.prefix_path;
    is_stderr_tty = os_is_tty(os_get_standard_stream(StandardStream::Error));

#if defined(_WIN32)
#if defined(__x86_64__)
    let dll_filename = 
#if defined(__x86_64__)
        SOs("clang_rt.asan_dynamic-x86_64.dll");
#elif defined(__aarch64__)
        SOs("clang_rt.asan_dynamic-aarch64.dll");
#else
#pragma error
#endif
    StringOs original_dll_parts[] = {
        toolchain_path,
        SOs("/lib/clang/21/lib/windows/"),
        dll_filename,
    };
    let original_asan_dll = string_os_join_arena(arenas[BATCH_ARENA_GENERAL], BUSTER_ARRAY_TO_SLICE(StringOsSlice, original_dll_parts), true);
    let target_strings = target_to_split_string_os(target_native);
    StringOs target_native_dir_path_parts[] = {
        SOs("build/"),
        target_strings.s[0],
        SOs("-"),
        target_strings.s[1],
        SOs("-"),
        target_strings.s[2],
    };
    let target_native_dir_path = string_os_join_arena(arenas[BATCH_ARENA_GENERAL], BUSTER_ARRAY_TO_SLICE(StringOsSlice, target_native_dir_path_parts), true);
    os_make_directory(target_native_dir_path);
    StringOs destination_dll_parts[] = {
        target_native_dir_path,
        SOs("/"),
        dll_filename,
    };
    let destination_asan_dll = string_os_join_arena(arenas[BATCH_ARENA_GENERAL], BUSTER_ARRAY_TO_SLICE(StringOsSlice, destination_dll_parts), true);
    file_copy((CopyFileArguments){ .original_path = original_asan_dll, .new_path = destination_asan_dll });
#endif
#endif

    ProcessResult result = ProcessResult::Success;

    u64 fuzz_time_seconds = build_integer_option_get_unsigned(BuildIntegerOption::BUILD_OPTION_INTEGER_FUZZ_DURATION_SECONDS);
    if (fuzz_time_seconds == 0)
    {
        bool ci = flag_get(program_state->input.flags, ProgramFlag::Ci);
        fuzz_time_seconds = ci ? 10 : 2;

        if (ci)
        {
            if (!build_flag_get(BuildFlag::BUILD_OPTION_FLAG_SELF_HOSTED))
            {
                fuzz_time_seconds = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_MAIN_BRANCH) ? 360 : fuzz_time_seconds;
            }
        }
    }

    bool is_test = build_program_state.command == BuildCommand::BUILD_COMMAND_TEST_ALL || build_program_state.command == BuildCommand::BUILD_COMMAND_TEST;

    if (build_program_state.command == BuildCommand::BUILD_COMMAND_TEST_ALL)
    {
        u64 succeeded_configuration_run = 0;
        u64 configuration_run = 0;

        let is_arm64_windows = target_native.cpu_arch == CpuArch::CPU_ARCH_AARCH64 && target_native.os == OperatingSystem::OPERATING_SYSTEM_WINDOWS;
        let is_windows = target_native.os == OperatingSystem::OPERATING_SYSTEM_WINDOWS;

        for (u64 fuzz = 0; fuzz < 1 + (!is_arm64_windows); fuzz += 1)
        {
            for (u64 optimize = 0; optimize < 2; optimize += 1)
            {
                for (u64 sanitize = 0; sanitize < 2; sanitize += 1)
                {
                    if (fuzz && !sanitize && is_windows)
                    {
                        continue;
                    }

                    for (u64 _has_debug_information = 0; _has_debug_information < 2; _has_debug_information += 1)
                    {
                        bool has_debug_information = !_has_debug_information;
                        for (u64 unity_build = 0; unity_build < 2; unity_build += 1)
                        {
                            string8_print(S8("================================\n"));
                            string8_print(S8("START\n"));
                            string8_print(S8("{S8}: {u64}\n"), S8("Fuzz"), fuzz);
                            string8_print(S8("{S8}: {u64}\n"), S8("Optimize"), optimize);
                            string8_print(S8("{S8}: {u64}\n"), S8("Sanitize"), sanitize);
                            string8_print(S8("{S8}: {u64}\n"), S8("Has debug information: "), has_debug_information);
                            string8_print(S8("{S8}: {u64}\n"), S8("Unity build"), unity_build);
                            string8_print(S8("================================\n"));

                            // system("rm -rf build");
                            os_make_directory(SOs("build"));

                            u64 arena_positions[(u64)BatchArena::Count];
                            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
                            {
                                arena_positions[i] = arenas[i]->position;
                            }

                            BatchTestConfiguration configuration = {
                                .function_optimization_log = build_string_option_get(BuildStringOption::BUILD_OPTION_STRING_PRINT_FUNCTION_OPTIMIZATION),
                                .optimize = optimize,
                                .fuzz = fuzz,
                                .sanitize = sanitize,
                                .has_debug_information = has_debug_information,
                                .unity_build = unity_build,
                                .just_preprocessor = 0,
                                .fuzz_time_seconds = fuzz_time_seconds,
                                .time_build = 0,
                                .include_tests = is_test,
                            };
                            memcpy(configuration.arenas, arenas, sizeof(arenas));

                            let run_result = single_run(&configuration);

                            bool success = run_result.process == ProcessResult::Success &&
                                run_result.module_test_count == run_result.succeeded_module_test_count && 
                                run_result.unit_test_count == run_result.succeeded_unit_test_count && 
                                run_result.external_test_count == run_result.succeeded_external_test_count;
                            if (!success)
                            {
                                string8_print(S8("================================\n"));
                                string8_print(S8("{S8}: {u32}\n"), S8("Fuzz"), fuzz);
                                string8_print(S8("{S8}: {u32}\n"), S8("Optimize"), optimize);
                                string8_print(S8("{S8}: {u32}\n"), S8("Sanitize"), sanitize);
                                string8_print(S8("{S8}: {u32}\n"), S8("Has debug information: "), has_debug_information);
                                string8_print(S8("{S8}: {u32}\n"), S8("Unity build"), unity_build);
                                string8_print(S8("{S8}\n"), success ? S8("SUCCEEDED") : S8("FAILED"));
                                string8_print(S8("================================\n"));
                            }

                            succeeded_configuration_run += success;
                            configuration_run += 1;

                            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
                            {
                                arenas[i]->position = arena_positions[i];
                            }
                        }
                    }
                }
            }
        }

        result = succeeded_configuration_run == configuration_run ? ProcessResult::Success : ProcessResult::Failed;
    }
    else
    {
        BatchTestConfiguration configuration = {
            .function_optimization_log = build_string_option_get(BuildStringOption::BUILD_OPTION_STRING_PRINT_FUNCTION_OPTIMIZATION),
            .optimize = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_OPTIMIZE),
            .fuzz = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_FUZZ),
            .sanitize = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_SANITIZE),
            .has_debug_information = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_HAS_DEBUG_INFORMATION),
            .unity_build = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_UNITY_BUILD),
            .just_preprocessor = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_JUST_PREPROCESSOR),
            .fuzz_time_seconds = fuzz_time_seconds,
            .time_build = build_flag_get(BuildFlag::BUILD_OPTION_FLAG_TIME_COMPILATION),
            .include_tests = is_test,
        };
        memcpy(configuration.arenas, arenas, sizeof(arenas));
        let run_result = single_run(&configuration);
        result = run_result.process;

        if (result == ProcessResult::Success)
        {
            bool success = run_result.unit_test_count == run_result.succeeded_unit_test_count && run_result.module_test_count == run_result.succeeded_module_test_count && run_result.external_test_count == run_result.succeeded_external_test_count;
            if (!success)
            {
                result = ProcessResult::Failed;
            }
        }
    }

    return result;
}
