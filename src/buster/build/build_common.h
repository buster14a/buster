#pragma once

#include <buster/string_os.h>
#include <buster/target.h>

STRUCT(BuildTarget)
{
    Target* pointer;
    StringOs string;
    StringOs march_string;
    StringOs directory_path;
};

STRUCT(CompileLinkOptions)
{
    StringOs clang_path;
    StringOs xc_sdk_path;
    StringOs destination_path;
    StringOs* source_paths;
    u64 source_count;
    BuildTarget* target;

    u64 optimize:1;
    u64 fuzz:1;
    u64 time_build:1;
    u64 has_debug_information:1;
    u64 unity_build:1;
    u64 use_io_ring:1;
    u64 use_graphics:1;
    u64 just_preprocessor:1;
    u64 sanitize:1;
    u64 include_tests:1;
    u64 force_color:1;
    u64 compile:1;
    u64 link:1;
    u64 reserved:51;
};

BUSTER_DECL BuildTarget build_target_native;

STRUCT(ToolchainInformation)
{
    StringOs prefix_path;
    StringOs clang_path;
    StringOs install_path;
    StringOs url;
};

STRUCT(LLVMVersion)
{
    u8 major;
    u8 minor;
    u8 revision;
    u8 reserved[5];
    StringOs string;
};

#define LLVM_VERSION(maj, min, rev) (LLVMVersion) { .major = (maj), .minor = (min), .revision = (rev), .string = SOs(#maj "." #min "." #rev) }
BUSTER_GLOBAL_LOCAL let current_llvm_version = LLVM_VERSION(21, 1, 8);

BUSTER_DECL StringOs vulkan_sdk_path;

BUSTER_DECL StringOsList build_compile_link_arguments(Arena* arena, const CompileLinkOptions * const options);
BUSTER_DECL ToolchainInformation toolchain_get_information(Arena* arena, LLVMVersion version);
