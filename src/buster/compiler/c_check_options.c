// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The semantic request's -f/-m admission boundary (#1125). Included by c_check_request.c, after the preprocessor.
// A preprocessor option's being known is NOT evidence that code generation implements it: predefines_neutral, for
// example, accepts retpolines because they change no macro. Only the owners and exact defaults below may pass the
// semantic scanner. The owning parser still validates values and resolves option order; admission is not acceptance.
// This is cold, once-per-request argv classification, alongside the existing scalar predefines option parsing. It
// changes neither source-processing kernels nor their scalar/vector selection, and allocates nothing.

static bool check_request_known_option(const char* spelling)
{
    const char* argument = predefines_alias(spelling);
    // These options already have a parser and a semantic consumer, or a resolved refusal in check_request_refusal.
    static const char* const handled[] = {
        "-fsyntax-only",
        "-flazy-system-headers",
        "-fno-lazy-system-headers",
        "-fhosted",
        "-ffreestanding",
        "-fbuiltin",
        "-fno-builtin",
        "-ftrigraphs",
        "-fno-trigraphs",
        "-fcaret-diagnostics",
        "-fno-caret-diagnostics",
        "-ferror-limit",
        "-fsigned-char",
        "-fno-unsigned-char",
        "-funsigned-char",
        "-fno-signed-char",
        "-fshort-wchar",
        "-fno-short-wchar",
        "-fgnu89-inline",
        "-fno-gnu89-inline",
        "-fPIC",
        "-fpic",
        "-fPIE",
        "-fpie",
        "-fno-PIC",
        "-fno-pic",
        "-fno-PIE",
        "-fno-pie",
        "-fasynchronous-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-funwind-tables",
        "-fno-unwind-tables",
        // The frame pointer on request (#177, predefines_frame_pointer): both tiers' frames take it.
        "-fomit-frame-pointer",
        "-fno-omit-frame-pointer",
        "-momit-leaf-frame-pointer",
        "-mno-omit-leaf-frame-pointer",
        "-fexceptions",
        "-fno-exceptions",
        "-fcommon",
        "-fno-common",
        "-fopenmp",
        "-fno-openmp",
        "-fsanitize-cfi-cross-dso",
        "-fno-sanitize-cfi-cross-dso",
        "-fdefine-target-os-macros",
        "-fno-define-target-os-macros",
        "-msse4",
        "-mno-sse4",
        "-m80387",
        "-mno-80387",
        "-mapxf",
        "-mno-apxf",
        "-mgeneral-regs-only",
        "-mlong-double-64",
        "-mlong-double-80",
        "-mlong-double-128",
    };
    // These grammars have explicit value parsers. In particular, coverage's '=' does NOT admit '-coverage-*', and
    // accepting a validated target feature does not admit the preprocessor's macro-neutral machine options.
    static const char* const values[] = {
        "-ferror-limit=",
        "-fgnuc-version=",
        "-fexec-charset=",
        "-finput-charset=",
        "-ffp-eval-method=",
        "-fsanitize=",
        "-fno-sanitize=",
        "-fsanitize-ignore-for-ubsan-feature=",
        "-fsanitize-coverage=",
        "-fno-sanitize-coverage=",
        "-fopenmp=",
        "-fopenmp-version=",
        "-march=",
        "-mtune=",
        "-mabi=",
        "-mapx-features=",
        "-mno-apx-features=",
    };
    // Exact compatibility controls: existing inactive/default behavior, permissions not obligations to optimize,
    // and presentation or C-inapplicable controls. No generic -fno-/-mno- or diagnostics-family escape hatch.
    // C exceptions above are not merely a no-op: they request the implemented unwind tables.
    static const char* const compatible[] = {
        "-ftrivial-auto-var-init=uninitialized",
        "-fzero-call-used-regs=skip",
        "-mcmodel=small",
        "-fno-split-stack",
        "-fno-trapv",
        "-fno-stack-clash-protection",
        "-fno-stack-check",
        "-fno-instrument-functions",
        "-fno-profile-arcs",
        "-fno-test-coverage",
        "-fno-profile-generate",
        "-fno-profile-instr-generate",
        "-fno-lto",
        "-fno-blocks",
        "-fplt",
        "-fwrapv",
        "-fno-wrapv",
        "-fstrict-overflow",
        "-fno-strict-overflow",
        "-fstrict-aliasing",
        "-fno-strict-aliasing",
        "-fno-semantic-interposition",
        "-ffp-contract=off",
        "-frtti",
        "-fno-rtti",
        "-fdwarf2-cfi-asm",
        "-fno-dwarf2-cfi-asm",
        "-fdiagnostics-color",
        "-fno-diagnostics-color",
        "-fdiagnostics-color=always",
        "-fdiagnostics-color=never",
        "-fdiagnostics-color=auto",
        "-fcolor-diagnostics",
        "-fno-color-diagnostics",
        "-mfpmath=sse",
        "-m64",
        "-mred-zone",
        "-mno-ms-bitfields",
        "-mno-rtd",
        "-mno-iamcu",
    };
    bool known = predefines_unsupported(argument);
    // Reuse the frontend's family inventory, including its last-option-wins refusals, instead of shadowing it with
    // another copy. An unsupported enabled family still fails in pp_settle_families, but a later disable may win.
    for (uint32_t i = 0; !known && i < PP_FAMILY_COUNT; i += 1)
    {
        known = strcmp(argument, pp_families[i].on) == 0 || (pp_families[i].off != NULL && strcmp(argument, pp_families[i].off) == 0);
    }
    for (uint32_t i = 0; !known && i < BUSTER_ARRAY_LENGTH(handled); i += 1)
    {
        known = strcmp(argument, handled[i]) == 0;
    }
    for (uint32_t i = 0; !known && i < BUSTER_ARRAY_LENGTH(values); i += 1)
    {
        known = strncmp(argument, values[i], strlen(values[i])) == 0;
    }
    for (uint32_t i = 0; !known && i < BUSTER_ARRAY_LENGTH(compatible); i += 1)
    {
        known = strcmp(argument, compatible[i]) == 0;
    }
    // A function name is an open namespace, not an option family. The preprocessor records exactly that name.
    known |= strncmp(argument, "-fno-builtin-", 13) == 0 && argument[13] != 0;
    if (!known && argument[0] == '-' && argument[1] == 'm')
    {
        const char* name = strncmp(argument, "-mno-", 5) == 0 ? argument + 5 : argument + 2;
        const int32_t feature = predefines_feature_index(name, strlen(name));
        known = feature >= 0 && predefines_features[feature].flag == PREDEFINES_FLAG_PLAIN;
    }
    return known;
}
