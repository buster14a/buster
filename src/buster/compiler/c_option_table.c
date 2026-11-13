// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C driver's option spellings, by canonical ID (#1444's item 5). The loops that read a compile's arguments
// (main_information, main_preprocessing, check_driver_command, check_request_scan, pp_parse_options and its
// predefines_option and pp_take_family) compared each argument with strcmp against one spelling after another: about
// 950 strcmp and 115 strncmp calls in a one-line -c, which every compile pays. Each loop now looks an argument up once
// and compares IDs.
//
// - BUSTER_C_OPTIONS: each spelling those loops match whole, or as the prefix through an argument's first '=' (-march=,
//   -std=), with its ID's name, C_OPTION_<name>: the spelling in capitals, its leading dashes dropped, '-' as '_' and '='
//   as EQ (-fno-PIC's and -fno-pic's names say BIG and SMALL, -L's and -l's DIRECTORY and LIBRARY, "-" is
//   STANDARD_INPUT and "--" END_OF_OPTIONS). Sorted bytewise, as strcmp orders them: test-checker --requests checks it
//   (c_option_table_test.c).
// - c_option_spellings and c_option_offsets: the spellings, contiguous (a struct of char arrays, so that offsetof gives
//   each one's place at compile time), and each one's offset, the next one's giving its length. main.c's units share
//   the front end's copy (BUSTER_C_OPTION_DATA), so that a compile, whose loops run in three of them, reads one.
// - c_option_find: an argument's COptionMatch, the ID of its whole spelling and that of its spelling through its first
//   '=', by binary search; c_option_exact, the first alone.
// - c_option_starts and c_option_is: an argument against one literal, for the prefixes that have no '=' at their end
//   (-fno-builtin-, -Wl,, -fptrauth) and a loop's single spelling, without a call.
//
// An ID stands for its spelling: `match.exact == C_OPTION_X` exactly where strcmp(argument, "x") == 0, and
// `match.joined == C_OPTION_X_EQ` exactly where strncmp(argument, "x=", strlen("x=")) == 0, since "x" has no '='. So a
// loop's order of tests, and what it does, stay as they were with strings.
//
// It stays scalar: a lookup is a binary search of about 9 probes of a few bytes each, once per argument per loop, on a
// command line's few arguments, about 1,300 instructions in all of a one-line -c's (Callgrind, #1444); the sets the
// loops test an ID against are arrays of 16-bit IDs, which clang compares a vector at a time.
#ifndef BUSTER_C_OPTION_TABLE_INCLUDED
#define BUSTER_C_OPTION_TABLE_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BUSTER_C_OPTIONS(ENTRY) \
    ENTRY(STANDARD_INPUT, "-") \
    ENTRY(END_OF_OPTIONS, "--") \
    ENTRY(ATTESTED_EQ, "--attested=") \
    ENTRY(BUSTER_HEADERS, "--buster-headers") \
    ENTRY(CHECK_TRANSITIONS, "--check-transitions") \
    ENTRY(CLANG_FEATURES, "--clang-features") \
    ENTRY(COVERAGE, "--coverage") \
    ENTRY(DEFERRED_BODIES, "--deferred-bodies") \
    ENTRY(DUMP_STATIC, "--dump-static") \
    ENTRY(DUMP_TYPED, "--dump-typed") \
    ENTRY(EMBED_DIR_EQ, "--embed-dir=") \
    ENTRY(HELPER_FORMS_EQ, "--helper-forms=") \
    ENTRY(HELPER_REPORT, "--helper-report") \
    ENTRY(ID_ORDER, "--id-order") \
    ENTRY(ID_ORDER_EQ, "--id-order=") \
    ENTRY(INTEL, "--intel") \
    ENTRY(LANE_CPUS, "--lane-cpus") \
    ENTRY(LANE_CPUS_EQ, "--lane-cpus=") \
    ENTRY(LANES, "--lanes") \
    ENTRY(LANES_EQ, "--lanes=") \
    ENTRY(LOCATION_LIMIT, "--location-limit") \
    ENTRY(LOWER_ALL, "--lower-all") \
    ENTRY(NO_DEFERRED_BODIES, "--no-deferred-bodies") \
    ENTRY(POISON_ARENAS, "--poison-arenas") \
    ENTRY(PREDEFINES, "--predefines") \
    ENTRY(SCALAR, "--scalar") \
    ENTRY(SHARED_PARTS, "--shared-parts") \
    ENTRY(TARGET_EQ, "--target=") \
    ENTRY(TIER0_NO_INLINE, "--tier0-no-inline") \
    ENTRY(TIER1, "--tier1") \
    ENTRY(TIER1_FUNCTION_SIZES, "--tier1-function-sizes") \
    ENTRY(TIER1_GVN, "--tier1-gvn") \
    ENTRY(TIER1_NO_INLINE, "--tier1-no-inline") \
    ENTRY(TIER1_NO_LIFETIMES, "--tier1-no-lifetimes") \
    ENTRY(TIER1_NO_REMATERIALIZE, "--tier1-no-rematerialize") \
    ENTRY(TIER1_REFUSALS, "--tier1-refusals") \
    ENTRY(TIER1_REPORT, "--tier1-report") \
    ENTRY(TIER1_SCHEDULE_EQ, "--tier1-schedule=") \
    ENTRY(TIER1_STRICT, "--tier1-strict") \
    ENTRY(TIME_STAGES, "--time-stages") \
    ENTRY(UNITS_MEMORY_EQ, "--units-memory=") \
    ENTRY(VERIFY, "--verify") \
    ENTRY(VERSION, "--version") \
    ENTRY(WINDOW_BYTES, "--window-bytes") \
    ENTRY(WORKER_BUDGET, "--worker-budget") \
    ENTRY(WORKER_BUDGET_EQ, "--worker-budget=") \
    ENTRY(D, "-D") \
    ENTRY(E, "-E") \
    ENTRY(H, "-H") \
    ENTRY(I, "-I") \
    ENTRY(L_DIRECTORY, "-L") \
    ENTRY(M, "-M") \
    ENTRY(MD, "-MD") \
    ENTRY(MF, "-MF") \
    ENTRY(MM, "-MM") \
    ENTRY(MMD, "-MMD") \
    ENTRY(MP, "-MP") \
    ENTRY(MQ, "-MQ") \
    ENTRY(MT, "-MT") \
    ENTRY(O0, "-O0") \
    ENTRY(P, "-P") \
    ENTRY(U, "-U") \
    ENTRY(WALL, "-Wall") \
    ENTRY(WERROR, "-Werror") \
    ENTRY(WERROR_IMPLICIT_FUNCTION_DECLARATION, "-Werror-implicit-function-declaration") \
    ENTRY(WERROR_EQ_ALL, "-Werror=all") \
    ENTRY(WERROR_EQ_IMPLICIT, "-Werror=implicit") \
    ENTRY(WERROR_EQ_IMPLICIT_FUNCTION_DECLARATION, "-Werror=implicit-function-declaration") \
    ENTRY(WERROR_EQ_RETURN_TYPE, "-Werror=return-type") \
    ENTRY(WEVERYTHING, "-Weverything") \
    ENTRY(WFATAL_ERRORS, "-Wfatal-errors") \
    ENTRY(WIMPLICIT, "-Wimplicit") \
    ENTRY(WIMPLICIT_FUNCTION_DECLARATION, "-Wimplicit-function-declaration") \
    ENTRY(WL, "-Wl") \
    ENTRY(WL_EQ, "-Wl=") \
    ENTRY(WNO_ALL, "-Wno-all") \
    ENTRY(WNO_ERROR, "-Wno-error") \
    ENTRY(WNO_ERROR_IMPLICIT_FUNCTION_DECLARATION, "-Wno-error-implicit-function-declaration") \
    ENTRY(WNO_ERROR_EQ_ALL, "-Wno-error=all") \
    ENTRY(WNO_ERROR_EQ_IMPLICIT, "-Wno-error=implicit") \
    ENTRY(WNO_ERROR_EQ_IMPLICIT_FUNCTION_DECLARATION, "-Wno-error=implicit-function-declaration") \
    ENTRY(WNO_ERROR_EQ_RETURN_TYPE, "-Wno-error=return-type") \
    ENTRY(WNO_EVERYTHING, "-Wno-everything") \
    ENTRY(WNO_FATAL_ERRORS, "-Wno-fatal-errors") \
    ENTRY(WNO_IMPLICIT, "-Wno-implicit") \
    ENTRY(WNO_IMPLICIT_FUNCTION_DECLARATION, "-Wno-implicit-function-declaration") \
    ENTRY(WNO_RETURN_TYPE, "-Wno-return-type") \
    ENTRY(WNO_SYSTEM_HEADERS, "-Wno-system-headers") \
    ENTRY(WRETURN_TYPE, "-Wreturn-type") \
    ENTRY(WSYSTEM_HEADERS, "-Wsystem-headers") \
    ENTRY(XLINKER, "-Xlinker") \
    ENTRY(ANSI, "-ansi") \
    ENTRY(C, "-c") \
    ENTRY(DM, "-dM") \
    ENTRY(DUMPFULLVERSION, "-dumpfullversion") \
    ENTRY(DUMPMACHINE, "-dumpmachine") \
    ENTRY(DUMPVERSION, "-dumpversion") \
    ENTRY(FPIC_BIG, "-fPIC") \
    ENTRY(FPIE_BIG, "-fPIE") \
    ENTRY(FAPPLE_KEXT, "-fapple-kext") \
    ENTRY(FAPPROX_FUNC, "-fapprox-func") \
    ENTRY(FASM, "-fasm") \
    ENTRY(FASSOCIATIVE_MATH, "-fassociative-math") \
    ENTRY(FASYNCHRONOUS_UNWIND_TABLES, "-fasynchronous-unwind-tables") \
    ENTRY(FBLOCKS, "-fblocks") \
    ENTRY(FBORLAND_EXTENSIONS, "-fborland-extensions") \
    ENTRY(FBUILTIN, "-fbuiltin") \
    ENTRY(FCARET_DIAGNOSTICS, "-fcaret-diagnostics") \
    ENTRY(FCF_PROTECTION, "-fcf-protection") \
    ENTRY(FCF_PROTECTION_EQ, "-fcf-protection=") \
    ENTRY(FCF_PROTECTION_EQ_NONE, "-fcf-protection=none") \
    ENTRY(FCOLOR_DIAGNOSTICS, "-fcolor-diagnostics") \
    ENTRY(FCOMMON, "-fcommon") \
    ENTRY(FCONSTANT_CFSTRINGS, "-fconstant-cfstrings") \
    ENTRY(FCS_PROFILE_GENERATE, "-fcs-profile-generate") \
    ENTRY(FCS_PROFILE_GENERATE_EQ, "-fcs-profile-generate=") \
    ENTRY(FCS_PROFILE_USE, "-fcs-profile-use") \
    ENTRY(FDATA_SECTIONS, "-fdata-sections") \
    ENTRY(FDECLSPEC, "-fdeclspec") \
    ENTRY(FDEFER_TS, "-fdefer-ts") \
    ENTRY(FDEFINE_TARGET_OS_MACROS, "-fdefine-target-os-macros") \
    ENTRY(FDIAGNOSTICS_COLOR, "-fdiagnostics-color") \
    ENTRY(FDIAGNOSTICS_COLOR_EQ_ALWAYS, "-fdiagnostics-color=always") \
    ENTRY(FDIAGNOSTICS_COLOR_EQ_AUTO, "-fdiagnostics-color=auto") \
    ENTRY(FDIAGNOSTICS_COLOR_EQ_NEVER, "-fdiagnostics-color=never") \
    ENTRY(FDIGRAPHS, "-fdigraphs") \
    ENTRY(FDIRECTIVES_ONLY, "-fdirectives-only") \
    ENTRY(FDOLLARS_IN_IDENTIFIERS, "-fdollars-in-identifiers") \
    ENTRY(FDWARF_EXCEPTIONS, "-fdwarf-exceptions") \
    ENTRY(FDWARF2_CFI_ASM, "-fdwarf2-cfi-asm") \
    ENTRY(FERROR_LIMIT, "-ferror-limit") \
    ENTRY(FERROR_LIMIT_EQ, "-ferror-limit=") \
    ENTRY(FEXCEPTIONS, "-fexceptions") \
    ENTRY(FEXEC_CHARSET_EQ, "-fexec-charset=") \
    ENTRY(FFAST_MATH, "-ffast-math") \
    ENTRY(FFILE_PREFIX_MAP_EQ, "-ffile-prefix-map=") \
    ENTRY(FFINITE_MATH_ONLY, "-ffinite-math-only") \
    ENTRY(FFIXED_POINT, "-ffixed-point") \
    ENTRY(FFP_CONTRACT_EQ_OFF, "-ffp-contract=off") \
    ENTRY(FFP_EVAL_METHOD_EQ, "-ffp-eval-method=") \
    ENTRY(FFP_EXCEPTION_BEHAVIOR_EQ, "-ffp-exception-behavior=") \
    ENTRY(FFP_MODEL_EQ, "-ffp-model=") \
    ENTRY(FFREESTANDING, "-ffreestanding") \
    ENTRY(FFUNCTION_SECTIONS, "-ffunction-sections") \
    ENTRY(FGNU_KEYWORDS, "-fgnu-keywords") \
    ENTRY(FGNU89_INLINE, "-fgnu89-inline") \
    ENTRY(FGNUC_VERSION_EQ, "-fgnuc-version=") \
    ENTRY(FHONOR_INFINITIES, "-fhonor-infinities") \
    ENTRY(FHONOR_NANS, "-fhonor-nans") \
    ENTRY(FHOSTED, "-fhosted") \
    ENTRY(FIMPLICIT_MODULE_MAPS, "-fimplicit-module-maps") \
    ENTRY(FINLINE, "-finline") \
    ENTRY(FINLINE_FUNCTIONS, "-finline-functions") \
    ENTRY(FINLINE_HINT_FUNCTIONS, "-finline-hint-functions") \
    ENTRY(FINPUT_CHARSET_EQ, "-finput-charset=") \
    ENTRY(FINSTRUMENT_FUNCTIONS, "-finstrument-functions") \
    ENTRY(FINSTRUMENT_FUNCTIONS_AFTER_INLINING, "-finstrument-functions-after-inlining") \
    ENTRY(FKEEP_SYSTEM_INCLUDES, "-fkeep-system-includes") \
    ENTRY(FLAZY_SYSTEM_HEADERS, "-flazy-system-headers") \
    ENTRY(FLTO, "-flto") \
    ENTRY(FLTO_EQ, "-flto=") \
    ENTRY(FLTO_EQ_AUTO, "-flto=auto") \
    ENTRY(FLTO_EQ_JOBSERVER, "-flto=jobserver") \
    ENTRY(FMACRO_PREFIX_MAP_EQ, "-fmacro-prefix-map=") \
    ENTRY(FMATH_ERRNO, "-fmath-errno") \
    ENTRY(FMINIMIZE_WHITESPACE, "-fminimize-whitespace") \
    ENTRY(FMODULE_MAP_FILE_EQ, "-fmodule-map-file=") \
    ENTRY(FMODULE_NAME_EQ, "-fmodule-name=") \
    ENTRY(FMODULES, "-fmodules") \
    ENTRY(FMS_COMPATIBILITY, "-fms-compatibility") \
    ENTRY(FMS_COMPATIBILITY_VERSION_EQ, "-fms-compatibility-version=") \
    ENTRY(FMS_EXTENSIONS, "-fms-extensions") \
    ENTRY(FNO_PIC_BIG, "-fno-PIC") \
    ENTRY(FNO_PIE_BIG, "-fno-PIE") \
    ENTRY(FNO_APPROX_FUNC, "-fno-approx-func") \
    ENTRY(FNO_ASM, "-fno-asm") \
    ENTRY(FNO_ASSOCIATIVE_MATH, "-fno-associative-math") \
    ENTRY(FNO_ASYNCHRONOUS_UNWIND_TABLES, "-fno-asynchronous-unwind-tables") \
    ENTRY(FNO_BLOCKS, "-fno-blocks") \
    ENTRY(FNO_BORLAND_EXTENSIONS, "-fno-borland-extensions") \
    ENTRY(FNO_BUILTIN, "-fno-builtin") \
    ENTRY(FNO_CARET_DIAGNOSTICS, "-fno-caret-diagnostics") \
    ENTRY(FNO_COLOR_DIAGNOSTICS, "-fno-color-diagnostics") \
    ENTRY(FNO_COMMON, "-fno-common") \
    ENTRY(FNO_CONSTANT_CFSTRINGS, "-fno-constant-cfstrings") \
    ENTRY(FNO_DATA_SECTIONS, "-fno-data-sections") \
    ENTRY(FNO_DECLSPEC, "-fno-declspec") \
    ENTRY(FNO_DEFER_TS, "-fno-defer-ts") \
    ENTRY(FNO_DEFINE_TARGET_OS_MACROS, "-fno-define-target-os-macros") \
    ENTRY(FNO_DIAGNOSTICS_COLOR, "-fno-diagnostics-color") \
    ENTRY(FNO_DIGRAPHS, "-fno-digraphs") \
    ENTRY(FNO_DIRECTIVES_ONLY, "-fno-directives-only") \
    ENTRY(FNO_DOLLARS_IN_IDENTIFIERS, "-fno-dollars-in-identifiers") \
    ENTRY(FNO_DWARF2_CFI_ASM, "-fno-dwarf2-cfi-asm") \
    ENTRY(FNO_EXCEPTIONS, "-fno-exceptions") \
    ENTRY(FNO_FAST_MATH, "-fno-fast-math") \
    ENTRY(FNO_FINITE_MATH_ONLY, "-fno-finite-math-only") \
    ENTRY(FNO_FIXED_POINT, "-fno-fixed-point") \
    ENTRY(FNO_FUNCTION_SECTIONS, "-fno-function-sections") \
    ENTRY(FNO_GNU_KEYWORDS, "-fno-gnu-keywords") \
    ENTRY(FNO_GNU89_INLINE, "-fno-gnu89-inline") \
    ENTRY(FNO_HONOR_INFINITIES, "-fno-honor-infinities") \
    ENTRY(FNO_HONOR_NANS, "-fno-honor-nans") \
    ENTRY(FNO_IMPLICIT_MODULE_MAPS, "-fno-implicit-module-maps") \
    ENTRY(FNO_INLINE, "-fno-inline") \
    ENTRY(FNO_INLINE_FUNCTIONS, "-fno-inline-functions") \
    ENTRY(FNO_INSTRUMENT_FUNCTIONS, "-fno-instrument-functions") \
    ENTRY(FNO_KEEP_SYSTEM_INCLUDES, "-fno-keep-system-includes") \
    ENTRY(FNO_LAZY_SYSTEM_HEADERS, "-fno-lazy-system-headers") \
    ENTRY(FNO_LTO, "-fno-lto") \
    ENTRY(FNO_MATH_ERRNO, "-fno-math-errno") \
    ENTRY(FNO_MINIMIZE_WHITESPACE, "-fno-minimize-whitespace") \
    ENTRY(FNO_MODULES, "-fno-modules") \
    ENTRY(FNO_MS_COMPATIBILITY, "-fno-ms-compatibility") \
    ENTRY(FNO_MS_EXTENSIONS, "-fno-ms-extensions") \
    ENTRY(FNO_OMIT_FRAME_POINTER, "-fno-omit-frame-pointer") \
    ENTRY(FNO_OPENMP, "-fno-openmp") \
    ENTRY(FNO_PACK_STRUCT, "-fno-pack-struct") \
    ENTRY(FNO_PASCAL_STRINGS, "-fno-pascal-strings") \
    ENTRY(FNO_PIC_SMALL, "-fno-pic") \
    ENTRY(FNO_PIE_SMALL, "-fno-pie") \
    ENTRY(FNO_PLT, "-fno-plt") \
    ENTRY(FNO_PROFILE_ARCS, "-fno-profile-arcs") \
    ENTRY(FNO_PROFILE_GENERATE, "-fno-profile-generate") \
    ENTRY(FNO_PROFILE_INSTR_GENERATE, "-fno-profile-instr-generate") \
    ENTRY(FNO_RAW_STRING_LITERALS, "-fno-raw-string-literals") \
    ENTRY(FNO_RECIPROCAL_MATH, "-fno-reciprocal-math") \
    ENTRY(FNO_REWRITE_IMPORTS, "-fno-rewrite-imports") \
    ENTRY(FNO_REWRITE_INCLUDES, "-fno-rewrite-includes") \
    ENTRY(FNO_ROUNDING_MATH, "-fno-rounding-math") \
    ENTRY(FNO_RTTI, "-fno-rtti") \
    ENTRY(FNO_SANITIZE_CFI_CROSS_DSO, "-fno-sanitize-cfi-cross-dso") \
    ENTRY(FNO_SANITIZE_COVERAGE_EQ, "-fno-sanitize-coverage=") \
    ENTRY(FNO_SANITIZE_EQ, "-fno-sanitize=") \
    ENTRY(FNO_SEMANTIC_INTERPOSITION, "-fno-semantic-interposition") \
    ENTRY(FNO_SHORT_ENUMS, "-fno-short-enums") \
    ENTRY(FNO_SHORT_WCHAR, "-fno-short-wchar") \
    ENTRY(FNO_SIGNED_CHAR, "-fno-signed-char") \
    ENTRY(FNO_SIGNED_ZEROS, "-fno-signed-zeros") \
    ENTRY(FNO_SPLIT_STACK, "-fno-split-stack") \
    ENTRY(FNO_STACK_CHECK, "-fno-stack-check") \
    ENTRY(FNO_STACK_CLASH_PROTECTION, "-fno-stack-clash-protection") \
    ENTRY(FNO_STACK_PROTECTOR, "-fno-stack-protector") \
    ENTRY(FNO_STRICT_ALIASING, "-fno-strict-aliasing") \
    ENTRY(FNO_STRICT_OVERFLOW, "-fno-strict-overflow") \
    ENTRY(FNO_TEST_COVERAGE, "-fno-test-coverage") \
    ENTRY(FNO_TRAPPING_MATH, "-fno-trapping-math") \
    ENTRY(FNO_TRAPV, "-fno-trapv") \
    ENTRY(FNO_TRIGRAPHS, "-fno-trigraphs") \
    ENTRY(FNO_UNSAFE_MATH_OPTIMIZATIONS, "-fno-unsafe-math-optimizations") \
    ENTRY(FNO_UNSIGNED_CHAR, "-fno-unsigned-char") \
    ENTRY(FNO_UNWIND_TABLES, "-fno-unwind-tables") \
    ENTRY(FNO_USE_LINE_DIRECTIVES, "-fno-use-line-directives") \
    ENTRY(FNO_WRAPV, "-fno-wrapv") \
    ENTRY(FNO_WRAPV_POINTER, "-fno-wrapv-pointer") \
    ENTRY(FNO_ZOS_EXTENSIONS, "-fno-zos-extensions") \
    ENTRY(FOFFLOAD_VIA_LLVM, "-foffload-via-llvm") \
    ENTRY(FOMIT_FRAME_POINTER, "-fomit-frame-pointer") \
    ENTRY(FOPENACC, "-fopenacc") \
    ENTRY(FOPENMP, "-fopenmp") \
    ENTRY(FOPENMP_TARGETS_EQ, "-fopenmp-targets=") \
    ENTRY(FOPENMP_VERSION_EQ, "-fopenmp-version=") \
    ENTRY(FOPENMP_EQ, "-fopenmp=") \
    ENTRY(FPACK_STRUCT, "-fpack-struct") \
    ENTRY(FPACK_STRUCT_EQ, "-fpack-struct=") \
    ENTRY(FPASCAL_STRINGS, "-fpascal-strings") \
    ENTRY(FPATCHABLE_FUNCTION_ENTRY_EQ, "-fpatchable-function-entry=") \
    ENTRY(FPCC_STRUCT_RETURN, "-fpcc-struct-return") \
    ENTRY(FPIC_SMALL, "-fpic") \
    ENTRY(FPIE_SMALL, "-fpie") \
    ENTRY(FPLT, "-fplt") \
    ENTRY(FPREPROCESSED, "-fpreprocessed") \
    ENTRY(FPROFILE_ARCS, "-fprofile-arcs") \
    ENTRY(FPROFILE_GENERATE, "-fprofile-generate") \
    ENTRY(FPROFILE_GENERATE_COLD_FUNCTION_COVERAGE, "-fprofile-generate-cold-function-coverage") \
    ENTRY(FPROFILE_GENERATE_COLD_FUNCTION_COVERAGE_EQ, "-fprofile-generate-cold-function-coverage=") \
    ENTRY(FPROFILE_GENERATE_EQ, "-fprofile-generate=") \
    ENTRY(FPROFILE_INSTR_GENERATE, "-fprofile-instr-generate") \
    ENTRY(FPROFILE_INSTR_GENERATE_EQ, "-fprofile-instr-generate=") \
    ENTRY(FPROFILE_INSTR_USE, "-fprofile-instr-use") \
    ENTRY(FPROFILE_INSTR_USE_EQ, "-fprofile-instr-use=") \
    ENTRY(FPROFILE_USE, "-fprofile-use") \
    ENTRY(FPROFILE_USE_EQ, "-fprofile-use=") \
    ENTRY(FRAW_STRING_LITERALS, "-fraw-string-literals") \
    ENTRY(FRECIPROCAL_MATH, "-freciprocal-math") \
    ENTRY(FREG_STRUCT_RETURN, "-freg-struct-return") \
    ENTRY(FREWRITE_IMPORTS, "-frewrite-imports") \
    ENTRY(FREWRITE_INCLUDES, "-frewrite-includes") \
    ENTRY(FROUNDING_MATH, "-frounding-math") \
    ENTRY(FRTTI, "-frtti") \
    ENTRY(FSANITIZE_CFI_CROSS_DSO, "-fsanitize-cfi-cross-dso") \
    ENTRY(FSANITIZE_COVERAGE, "-fsanitize-coverage") \
    ENTRY(FSANITIZE_COVERAGE_EQ, "-fsanitize-coverage=") \
    ENTRY(FSANITIZE_IGNORE_FOR_UBSAN_FEATURE_EQ, "-fsanitize-ignore-for-ubsan-feature=") \
    ENTRY(FSANITIZE_EQ, "-fsanitize=") \
    ENTRY(FSEH_EXCEPTIONS, "-fseh-exceptions") \
    ENTRY(FSHORT_DOUBLE, "-fshort-double") \
    ENTRY(FSHORT_ENUMS, "-fshort-enums") \
    ENTRY(FSHORT_WCHAR, "-fshort-wchar") \
    ENTRY(FSIGNED_CHAR, "-fsigned-char") \
    ENTRY(FSIGNED_ZEROS, "-fsigned-zeros") \
    ENTRY(FSJLJ_EXCEPTIONS, "-fsjlj-exceptions") \
    ENTRY(FSTACK_CHECK, "-fstack-check") \
    ENTRY(FSTACK_CHECK_EQ, "-fstack-check=") \
    ENTRY(FSTACK_CLASH_PROTECTION, "-fstack-clash-protection") \
    ENTRY(FSTACK_PROTECTOR, "-fstack-protector") \
    ENTRY(FSTACK_PROTECTOR_ALL, "-fstack-protector-all") \
    ENTRY(FSTACK_PROTECTOR_EXPLICIT, "-fstack-protector-explicit") \
    ENTRY(FSTACK_PROTECTOR_STRONG, "-fstack-protector-strong") \
    ENTRY(FSTRICT_ALIASING, "-fstrict-aliasing") \
    ENTRY(FSTRICT_OVERFLOW, "-fstrict-overflow") \
    ENTRY(FSYNTAX_ONLY, "-fsyntax-only") \
    ENTRY(FTEST_COVERAGE, "-ftest-coverage") \
    ENTRY(FTLS_MODEL_EQ, "-ftls-model=") \
    ENTRY(FTRAPPING_MATH, "-ftrapping-math") \
    ENTRY(FTRIGRAPHS, "-ftrigraphs") \
    ENTRY(FTRIVIAL_AUTO_VAR_INIT_EQ_UNINITIALIZED, "-ftrivial-auto-var-init=uninitialized") \
    ENTRY(FUNSAFE_MATH_OPTIMIZATIONS, "-funsafe-math-optimizations") \
    ENTRY(FUNSIGNED_CHAR, "-funsigned-char") \
    ENTRY(FUNWIND_TABLES, "-funwind-tables") \
    ENTRY(FUSE_LINE_DIRECTIVES, "-fuse-line-directives") \
    ENTRY(FVECLIB_EQ, "-fveclib=") \
    ENTRY(FVISIBILITY, "-fvisibility") \
    ENTRY(FVISIBILITY_MS_COMPAT, "-fvisibility-ms-compat") \
    ENTRY(FVISIBILITY_EQ, "-fvisibility=") \
    ENTRY(FWASM_EXCEPTIONS, "-fwasm-exceptions") \
    ENTRY(FWIDE_EXEC_CHARSET_EQ, "-fwide-exec-charset=") \
    ENTRY(FWRAPV, "-fwrapv") \
    ENTRY(FWRAPV_POINTER, "-fwrapv-pointer") \
    ENTRY(FZERO_CALL_USED_REGS_EQ_SKIP, "-fzero-call-used-regs=skip") \
    ENTRY(FZOS_EXTENSIONS, "-fzos-extensions") \
    ENTRY(FZVECTOR, "-fzvector") \
    ENTRY(G, "-g") \
    ENTRY(G0, "-g0") \
    ENTRY(G1, "-g1") \
    ENTRY(G2, "-g2") \
    ENTRY(G3, "-g3") \
    ENTRY(GCODEVIEW, "-gcodeview") \
    ENTRY(GDBX, "-gdbx") \
    ENTRY(GDWARF, "-gdwarf") \
    ENTRY(GDWARF_2, "-gdwarf-2") \
    ENTRY(GDWARF_3, "-gdwarf-3") \
    ENTRY(GDWARF_4, "-gdwarf-4") \
    ENTRY(GDWARF_5, "-gdwarf-5") \
    ENTRY(GDWARF_6, "-gdwarf-6") \
    ENTRY(GDWARF32, "-gdwarf32") \
    ENTRY(GDWARF64, "-gdwarf64") \
    ENTRY(GFULL, "-gfull") \
    ENTRY(GGDB, "-ggdb") \
    ENTRY(GGDB0, "-ggdb0") \
    ENTRY(GGDB1, "-ggdb1") \
    ENTRY(GGDB2, "-ggdb2") \
    ENTRY(GGDB3, "-ggdb3") \
    ENTRY(GLINE_DIRECTIVES_ONLY, "-gline-directives-only") \
    ENTRY(GLINE_TABLES_ONLY, "-gline-tables-only") \
    ENTRY(GLLDB, "-glldb") \
    ENTRY(GMLT, "-gmlt") \
    ENTRY(GMODULES, "-gmodules") \
    ENTRY(GNO_MODULES, "-gno-modules") \
    ENTRY(GSCE, "-gsce") \
    ENTRY(GTOGGLE, "-gtoggle") \
    ENTRY(GUSED, "-gused") \
    ENTRY(IDIRAFTER, "-idirafter") \
    ENTRY(INCLUDE, "-include") \
    ENTRY(IQUOTE, "-iquote") \
    ENTRY(ISYSTEM, "-isystem") \
    ENTRY(L_LIBRARY, "-l") \
    ENTRY(M16, "-m16") \
    ENTRY(M32, "-m32") \
    ENTRY(M64, "-m64") \
    ENTRY(M80387, "-m80387") \
    ENTRY(MABI_EQ, "-mabi=") \
    ENTRY(MALIGN_BRANCH_BOUNDARY_EQ, "-malign-branch-boundary=") \
    ENTRY(MALIGN_BRANCH_EQ, "-malign-branch=") \
    ENTRY(MALIGN_DOUBLE, "-malign-double") \
    ENTRY(MAPX_FEATURES_EQ, "-mapx-features=") \
    ENTRY(MAPX_INLINE_ASM_USE_GPR32, "-mapx-inline-asm-use-gpr32") \
    ENTRY(MAPXF, "-mapxf") \
    ENTRY(MARCH_EQ, "-march=") \
    ENTRY(MASM_EQ, "-masm=") \
    ENTRY(MBRANCHES_WITHIN_32B_BOUNDARIES, "-mbranches-within-32B-boundaries") \
    ENTRY(MCMODEL_EQ, "-mcmodel=") \
    ENTRY(MCMODEL_EQ_SMALL, "-mcmodel=small") \
    ENTRY(MCMSE, "-mcmse") \
    ENTRY(MCONSTANT_CFSTRINGS, "-mconstant-cfstrings") \
    ENTRY(MCPU_EQ, "-mcpu=") \
    ENTRY(MEXECUTE_ONLY, "-mexecute-only") \
    ENTRY(MFENTRY, "-mfentry") \
    ENTRY(MFPMATH_EQ, "-mfpmath=") \
    ENTRY(MFPMATH_EQ_SSE, "-mfpmath=sse") \
    ENTRY(MFUNCTION_RETURN_EQ, "-mfunction-return=") \
    ENTRY(MGENERAL_REGS_ONLY, "-mgeneral-regs-only") \
    ENTRY(MHARDEN_SLS_EQ, "-mharden-sls=") \
    ENTRY(MIAMCU, "-miamcu") \
    ENTRY(MIMPLICIT_FLOAT, "-mimplicit-float") \
    ENTRY(MINCREMENTAL_LINKER_COMPATIBLE, "-mincremental-linker-compatible") \
    ENTRY(MINDIRECT_BRANCH_CS_PREFIX, "-mindirect-branch-cs-prefix") \
    ENTRY(MIPS1, "-mips1") \
    ENTRY(MIPS2, "-mips2") \
    ENTRY(MIPS3, "-mips3") \
    ENTRY(MIPS32, "-mips32") \
    ENTRY(MIPS32R2, "-mips32r2") \
    ENTRY(MIPS32R3, "-mips32r3") \
    ENTRY(MIPS32R5, "-mips32r5") \
    ENTRY(MIPS32R6, "-mips32r6") \
    ENTRY(MIPS4, "-mips4") \
    ENTRY(MIPS5, "-mips5") \
    ENTRY(MIPS64, "-mips64") \
    ENTRY(MIPS64R2, "-mips64r2") \
    ENTRY(MIPS64R3, "-mips64r3") \
    ENTRY(MIPS64R5, "-mips64r5") \
    ENTRY(MIPS64R6, "-mips64r6") \
    ENTRY(MKERNEL, "-mkernel") \
    ENTRY(MLARGE_DATA_THRESHOLD_EQ, "-mlarge-data-threshold=") \
    ENTRY(MLINKER_VERSION_EQ, "-mlinker-version=") \
    ENTRY(MLITTLE_ENDIAN, "-mlittle-endian") \
    ENTRY(MLONG_DOUBLE_128, "-mlong-double-128") \
    ENTRY(MLONG_DOUBLE_64, "-mlong-double-64") \
    ENTRY(MLONG_DOUBLE_80, "-mlong-double-80") \
    ENTRY(MLVI_CFI, "-mlvi-cfi") \
    ENTRY(MLVI_HARDENING, "-mlvi-hardening") \
    ENTRY(MMS_BITFIELDS, "-mms-bitfields") \
    ENTRY(MNO_80387, "-mno-80387") \
    ENTRY(MNO_APX_FEATURES_EQ, "-mno-apx-features=") \
    ENTRY(MNO_APXF, "-mno-apxf") \
    ENTRY(MNO_CONSTANT_CFSTRINGS, "-mno-constant-cfstrings") \
    ENTRY(MNO_EXECUTE_ONLY, "-mno-execute-only") \
    ENTRY(MNO_FP_RET_IN_387, "-mno-fp-ret-in-387") \
    ENTRY(MNO_GATHER, "-mno-gather") \
    ENTRY(MNO_IAMCU, "-mno-iamcu") \
    ENTRY(MNO_IMPLICIT_FLOAT, "-mno-implicit-float") \
    ENTRY(MNO_INCREMENTAL_LINKER_COMPATIBLE, "-mno-incremental-linker-compatible") \
    ENTRY(MNO_LVI_CFI, "-mno-lvi-cfi") \
    ENTRY(MNO_LVI_HARDENING, "-mno-lvi-hardening") \
    ENTRY(MNO_MS_BITFIELDS, "-mno-ms-bitfields") \
    ENTRY(MNO_OMIT_LEAF_FRAME_POINTER, "-mno-omit-leaf-frame-pointer") \
    ENTRY(MNO_OUTLINE, "-mno-outline") \
    ENTRY(MNO_PASCAL_STRINGS, "-mno-pascal-strings") \
    ENTRY(MNO_PURE_CODE, "-mno-pure-code") \
    ENTRY(MNO_RED_ZONE, "-mno-red-zone") \
    ENTRY(MNO_RELAX_ALL, "-mno-relax-all") \
    ENTRY(MNO_RESERVE_FRAME_POINTER_REG, "-mno-reserve-frame-pointer-reg") \
    ENTRY(MNO_RESTRICT_IT, "-mno-restrict-it") \
    ENTRY(MNO_RETPOLINE, "-mno-retpoline") \
    ENTRY(MNO_RETPOLINE_EXTERNAL_THUNK, "-mno-retpoline-external-thunk") \
    ENTRY(MNO_RTD, "-mno-rtd") \
    ENTRY(MNO_SCATTER, "-mno-scatter") \
    ENTRY(MNO_SESES, "-mno-seses") \
    ENTRY(MNO_SKIP_RAX_SETUP, "-mno-skip-rax-setup") \
    ENTRY(MNO_SOFT_FLOAT, "-mno-soft-float") \
    ENTRY(MNO_SPECULATIVE_LOAD_HARDENING, "-mno-speculative-load-hardening") \
    ENTRY(MNO_SSE4, "-mno-sse4") \
    ENTRY(MNO_STACK_ARG_PROBE, "-mno-stack-arg-probe") \
    ENTRY(MNO_STACKREALIGN, "-mno-stackrealign") \
    ENTRY(MNO_TLS_DIRECT_SEG_REFS, "-mno-tls-direct-seg-refs") \
    ENTRY(MNO_UNSAFE_FP_ATOMICS, "-mno-unsafe-fp-atomics") \
    ENTRY(MNO_VZEROUPPER, "-mno-vzeroupper") \
    ENTRY(MNO_ZVECTOR, "-mno-zvector") \
    ENTRY(MOMIT_LEAF_FRAME_POINTER, "-momit-leaf-frame-pointer") \
    ENTRY(MOUTLINE, "-moutline") \
    ENTRY(MPAD_MAX_PREFIX_SIZE_EQ, "-mpad-max-prefix-size=") \
    ENTRY(MPASCAL_STRINGS, "-mpascal-strings") \
    ENTRY(MPREFER_VECTOR_WIDTH_EQ, "-mprefer-vector-width=") \
    ENTRY(MPRINTF_KIND_EQ, "-mprintf-kind=") \
    ENTRY(MPURE_CODE, "-mpure-code") \
    ENTRY(MRECIP, "-mrecip") \
    ENTRY(MRECIP_EQ, "-mrecip=") \
    ENTRY(MRED_ZONE, "-mred-zone") \
    ENTRY(MREGPARM_EQ, "-mregparm=") \
    ENTRY(MRELAX_ALL, "-mrelax-all") \
    ENTRY(MRESERVE_FRAME_POINTER_REG, "-mreserve-frame-pointer-reg") \
    ENTRY(MRESTRICT_IT, "-mrestrict-it") \
    ENTRY(MRETPOLINE, "-mretpoline") \
    ENTRY(MRETPOLINE_EXTERNAL_THUNK, "-mretpoline-external-thunk") \
    ENTRY(MSESES, "-mseses") \
    ENTRY(MSKIP_RAX_SETUP, "-mskip-rax-setup") \
    ENTRY(MSOFT_FLOAT, "-msoft-float") \
    ENTRY(MSPECULATIVE_LOAD_HARDENING, "-mspeculative-load-hardening") \
    ENTRY(MSSE4, "-msse4") \
    ENTRY(MSTACK_ALIGNMENT_EQ, "-mstack-alignment=") \
    ENTRY(MSTACK_ARG_PROBE, "-mstack-arg-probe") \
    ENTRY(MSTACK_PROBE_SIZE_EQ, "-mstack-probe-size=") \
    ENTRY(MSTACK_PROTECTOR_GUARD_OFFSET_EQ, "-mstack-protector-guard-offset=") \
    ENTRY(MSTACK_PROTECTOR_GUARD_REG_EQ, "-mstack-protector-guard-reg=") \
    ENTRY(MSTACK_PROTECTOR_GUARD_SYMBOL_EQ, "-mstack-protector-guard-symbol=") \
    ENTRY(MSTACK_PROTECTOR_GUARD_EQ, "-mstack-protector-guard=") \
    ENTRY(MSTACKREALIGN, "-mstackrealign") \
    ENTRY(MTLS_DIALECT_EQ, "-mtls-dialect=") \
    ENTRY(MTLS_DIRECT_SEG_REFS, "-mtls-direct-seg-refs") \
    ENTRY(MTUNE_EQ, "-mtune=") \
    ENTRY(MUNSAFE_FP_ATOMICS, "-munsafe-fp-atomics") \
    ENTRY(MVZEROUPPER, "-mvzeroupper") \
    ENTRY(MX32, "-mx32") \
    ENTRY(MZVECTOR, "-mzvector") \
    ENTRY(NO_PTHREAD, "-no-pthread") \
    ENTRY(NOBUILTININC, "-nobuiltininc") \
    ENTRY(NOSTDINC, "-nostdinc") \
    ENTRY(O, "-o") \
    ENTRY(PEDANTIC, "-pedantic") \
    ENTRY(PEDANTIC_ERRORS, "-pedantic-errors") \
    ENTRY(PG, "-pg") \
    ENTRY(PIE, "-pie") \
    ENTRY(PIPE, "-pipe") \
    ENTRY(PRINT_FILE_NAME_EQ, "-print-file-name=") \
    ENTRY(PRINT_PROG_NAME_EQ, "-print-prog-name=") \
    ENTRY(PRINT_SEARCH_DIRS, "-print-search-dirs") \
    ENTRY(PTHREAD, "-pthread") \
    ENTRY(RPATH, "-rpath") \
    ENTRY(STATIC, "-static") \
    ENTRY(STATIC_PIE, "-static-pie") \
    ENTRY(STD_EQ, "-std=") \
    ENTRY(TARGET, "-target") \
    ENTRY(TRIGRAPHS, "-trigraphs") \
    ENTRY(UNDEF, "-undef") \
    ENTRY(V, "-v") \
    ENTRY(W, "-w") \
    ENTRY(X, "-x")

typedef enum COption
{
    C_OPTION_NONE,
#define BUSTER_C_OPTION_ENUM(name, spelling) C_OPTION_##name,
    BUSTER_C_OPTIONS(BUSTER_C_OPTION_ENUM)
#undef BUSTER_C_OPTION_ENUM
    C_OPTION_COUNT,
} COption;

// The spellings, one after another with their terminators: C_OPTION_NONE's is empty.
typedef struct COptionText COptionText;
struct COptionText
{
    char text_NONE[1];
#define BUSTER_C_OPTION_FIELD(name, spelling) char text_##name[sizeof(spelling)];
    BUSTER_C_OPTIONS(BUSTER_C_OPTION_FIELD)
#undef BUSTER_C_OPTION_FIELD
};

_Static_assert(sizeof(COptionText) <= UINT16_MAX, "an option's offset fits 16 bits");
_Static_assert(C_OPTION_COUNT <= UINT16_MAX, "an option's ID fits the loops' 16-bit sets");

#ifdef BUSTER_C_OPTION_DATA_ELSEWHERE
extern const COptionText c_option_spellings __attribute__((visibility("hidden")));
extern const uint16_t c_option_offsets[C_OPTION_COUNT + 1] __attribute__((visibility("hidden")));
#else
#ifndef BUSTER_C_OPTION_DATA
#define BUSTER_C_OPTION_DATA static
#endif
BUSTER_C_OPTION_DATA const COptionText c_option_spellings = {
    "",
#define BUSTER_C_OPTION_TEXT(name, spelling) spelling,
    BUSTER_C_OPTIONS(BUSTER_C_OPTION_TEXT)
#undef BUSTER_C_OPTION_TEXT
};

// Each ID's spelling's offset in c_option_spellings, and past the last one the struct's size.
BUSTER_C_OPTION_DATA const uint16_t c_option_offsets[C_OPTION_COUNT + 1] = {
    offsetof(COptionText, text_NONE),
#define BUSTER_C_OPTION_OFFSET(name, spelling) offsetof(COptionText, text_##name),
    BUSTER_C_OPTIONS(BUSTER_C_OPTION_OFFSET)
#undef BUSTER_C_OPTION_OFFSET
    sizeof(COptionText),
};
#endif

// What an argument is among the spellings (c_option_find).
typedef struct COptionMatch COptionMatch;
struct COptionMatch
{
    // The ID of the argument's whole spelling, or C_OPTION_NONE.
    COption exact;
    // The ID of its spelling through its first '=' (a joined value's option, such as -march=), or C_OPTION_NONE.
    COption joined;
};

// The spelling of `id`, with its terminator.
static __attribute__((unused)) const char* c_option_spelling(COption id)
{
    return (const char*)&c_option_spellings + c_option_offsets[id];
}

// The length of `id`'s spelling.
static __attribute__((unused)) uint32_t c_option_length(COption id)
{
    return (uint32_t)c_option_offsets[id + 1] - c_option_offsets[id] - 1;
}

// The ID whose spelling is the `length` bytes at `text`, or C_OPTION_NONE: a binary search, bytewise as strcmp orders.
static __attribute__((unused)) COption c_option_search(const char* text, size_t length)
{
    uint32_t low = C_OPTION_NONE + 1;
    uint32_t high = C_OPTION_COUNT;
    COption result = C_OPTION_NONE;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const char* entry = c_option_spelling((COption)middle);
        const size_t entry_length = c_option_length((COption)middle);
        const size_t common = length < entry_length ? length : entry_length;
        size_t i = 0;
        while (i < common && text[i] == entry[i])
        {
            i += 1;
        }
        const int order = i < common ? (int)(uint8_t)text[i] - (int)(uint8_t)entry[i] : length < entry_length ? -1 : length > entry_length ? 1 : 0;
        result = order == 0 ? (COption)middle : result;
        high = order < 0 ? middle : order == 0 ? low : high;
        low = order > 0 ? middle + 1 : low;
    }
    return result;
}

// `argument`'s IDs: its whole spelling's, and its spelling's through its first '='. An argument that doesn't start with
// '-' is no option's.
static __attribute__((unused)) COptionMatch c_option_find(const char* argument)
{
    COptionMatch result = {
        .exact = C_OPTION_NONE,
        .joined = C_OPTION_NONE,
    };
    if (argument[0] == '-')
    {
        size_t length = 0;
        size_t equals = 0;
        while (argument[length] != 0)
        {
            equals = equals == 0 && argument[length] == '=' ? length + 1 : equals;
            length += 1;
        }
        result.exact = c_option_search(argument, length);
        result.joined = equals != 0 ? c_option_search(argument, equals) : C_OPTION_NONE;
    }
    return result;
}

// `argument`'s whole spelling's ID, or C_OPTION_NONE.
static __attribute__((unused)) COption c_option_exact(const char* argument)
{
    COption result = C_OPTION_NONE;
    if (argument[0] == '-')
    {
        result = c_option_search(argument, strlen(argument));
    }
    return result;
}

// Whether `argument` starts with `prefix`, as strncmp(argument, prefix, strlen(prefix)) == 0.
static __attribute__((unused)) bool c_option_starts(const char* argument, const char* prefix)
{
    size_t i = 0;
    while (prefix[i] != 0 && argument[i] == prefix[i])
    {
        i += 1;
    }
    return prefix[i] == 0;
}

// Whether `argument` is `spelling`, as strcmp(argument, spelling) == 0.
static __attribute__((unused)) bool c_option_is(const char* argument, const char* spelling)
{
    size_t i = 0;
    while (spelling[i] != 0 && argument[i] == spelling[i])
    {
        i += 1;
    }
    return argument[i] == spelling[i];
}

#endif
