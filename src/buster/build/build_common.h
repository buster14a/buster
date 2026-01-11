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
    u64 has_debug_information:1;
    // u64 ci:1;
    u64 unity_build:1;
    u64 use_io_ring:1;
    u64 just_preprocessor:1;
    u64 compile:1;
    u64 link:1;
    u64 sanitize:1;
    u64 reserved:55;
};

BUSTER_GLOBAL_LOCAL StringOs enable_warning_flags[] = {
    SOs("-Wall"),
    SOs("-Wextra"),
    SOs("-Wpedantic"),
    SOs("-pedantic"),
    SOs("-Wconversion"),
    SOs("-Wstrict-overflow=5"),
    SOs("-Woverflow"),
    SOs("-Wshift-overflow"),
    SOs("-Walloca"),
    SOs("-Warray-bounds-pointer-arithmetic"),
    SOs("-Wassign-enum"),
    SOs("-Wbool-conversion"),
    SOs("-Wbool-operation"),
    SOs("-Wcomma"),
    SOs("-Wconditional-uninitialized"),
    SOs("-Wdangling"),
    SOs("-Wdouble-promotion"),
    SOs("-Wenum-compare-conditional"),
    SOs("-Wenum-too-large"),
    SOs("-Wexperimental-lifetime-safety"),
    SOs("-Wfixed-point-overflow"),
    SOs("-Wflag-enum"),
    SOs("-Wformat"),
    SOs("-Wfortify-source"),
    SOs("-Wfour-char-constants"),
    SOs("-Whigher-precision-for-complex-division"),
    SOs("-Wimplicit"),
    SOs("-Wimplicit-fallthrough"),
    SOs("-Wimplicit-fallthrough-per-function"),
    SOs("-Wimplicit-float-conversion"),
    SOs("-Wimplicit-int-conversion"),
    SOs("-Wimplicit-void-ptr-cast"),
    SOs("-Winfinite-recursion"),
    SOs("-Winvalid-utf8"),
    SOs("-Wlarge-by-value-copy"),
    SOs("-Wlinker-warnings"),
    SOs("-Wloop-analysis"),
    SOs("-Wmain"),
    SOs("-Wmisleading-indentation"),
    SOs("-Wmissing-braces"),
    SOs("-Wmissing-noreturn"),
    SOs("-Wnon-power-of-two-alignment"),
    SOs("-Woption-ignored"),
    SOs("-Woverlength-strings"),
    SOs("-Wpacked"),
    SOs("-Wpadded"),
    SOs("-Wparentheses"),
    SOs("-Wpedantic-macros"),
    SOs("-Wpointer-arith"),
    SOs("-Wpragma-pack"),
    SOs("-Wpragma-pack-suspicious-include"),
    SOs("-Wpragmas"),
    SOs("-Wread-only-types"),
    SOs("-Wredundant-parens"),
    SOs("-Wreserved-identifier"),
    SOs("-Wreserved-macro-identifier"),
    SOs("-Wreserved-module-identifier"),
    SOs("-Wself-assign"),
    SOs("-Wself-assign-field"),
    SOs("-Wshadow"),
    SOs("-Wshadow-all"),
    SOs("-Wshadow-field"),
    SOs("-Wshift-bool"),
    SOs("-Wshift-sign-overflow"),
    SOs("-Wsigned-enum-bitfield"),
    SOs("-Wtautological-compare"),
    SOs("-Wtype-limits"),
    SOs("-Wtautological-constant-in-range-compare"),
    SOs("-Wthread-safety"),
    SOs("-Wuninitialized"),
    SOs("-Wunaligned-access"),
    SOs("-Wunique-object-duplication"),
    SOs("-Wunreachable-code"),
    SOs("-Wunreachable-code-return"),
    SOs("-Wvector-conversion"),
};

BUSTER_GLOBAL_LOCAL StringOs disable_warning_flags[] = {
    SOs("-Wno-language-extension-token"),
    SOs("-Wno-gnu-auto-type"),
    SOs("-Wno-gnu-empty-struct"),
    SOs("-Wno-bitwise-instead-of-logical"),
    SOs("-Wno-unused-function"),
    SOs("-Wno-gnu-flexible-array-initializer"),
    SOs("-Wno-missing-field-initializers"),
    SOs("-Wno-pragma-once-outside-header"),
    SOs("-Wno-zero-length-array"),
    SOs("-Wno-gnu-zero-variadic-macro-arguments"),
    SOs("-Wno-gnu-statement-expression-from-macro-expansion"),
};

BUSTER_GLOBAL_LOCAL StringOs f_flags[] = {
    SOs("-fwrapv"),
    SOs("-fno-strict-aliasing"),
    SOs("-funsigned-char"),
    SOs("-fno-exceptions"),
    SOs("-fno-rtti"),
};

BUSTER_GLOBAL_LOCAL StringOs include_flags[] = {
    SOs("-Isrc"),
};

BUSTER_GLOBAL_LOCAL StringOs std_flags[] = {
    SOs("-std=gnu2x"),
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
BUSTER_GLOBAL_LOCAL let current_llvm_version = LLVM_VERSION(21, 1, 7);

BUSTER_DECL StringOsList build_compile_link_arguments(Arena* arena, const CompileLinkOptions * const options);
BUSTER_DECL ToolchainInformation toolchain_get_information(Arena* arena, LLVMVersion version);
