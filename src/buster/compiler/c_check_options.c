// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The semantic request's -f/-m admission boundary (#1125). Included by c_check_request.c, after the preprocessor.
// A preprocessor option's being known is NOT evidence that code generation implements it: predefines_neutral, for
// example, accepts retpolines because they change no macro. Only the owners and exact defaults below may pass the
// semantic scanner. The owning parser still validates values and resolves option order; admission is not acceptance.
// This is cold, once-per-request argv classification, alongside the existing scalar predefines option parsing. It
// changes neither source-processing kernels nor their scalar/vector selection, and allocates nothing.

// `spelled` is `spelling`'s option IDs (c_option_find); the lists below are IDs (c_option_table.c), the values' through
// their '='.
static bool check_request_known_option(const char* spelling, COptionMatch spelled)
{
    // An alias (predefines_alias) is known as the option it stands for.
    const char* argument = predefines_alias(spelling, spelled.exact);
    const COptionMatch match = argument != spelling ? c_option_find(argument) : spelled;
    // These options already have a parser and a semantic consumer, or a resolved refusal in check_request_refusal.
    static const uint16_t handled[] = {
        C_OPTION_FSYNTAX_ONLY,
        C_OPTION_FLAZY_SYSTEM_HEADERS,
        C_OPTION_FNO_LAZY_SYSTEM_HEADERS,
        C_OPTION_FHOSTED,
        C_OPTION_FFREESTANDING,
        C_OPTION_FBUILTIN,
        C_OPTION_FNO_BUILTIN,
        C_OPTION_FTRIGRAPHS,
        C_OPTION_FNO_TRIGRAPHS,
        C_OPTION_FCARET_DIAGNOSTICS,
        C_OPTION_FNO_CARET_DIAGNOSTICS,
        C_OPTION_FERROR_LIMIT,
        C_OPTION_FSIGNED_CHAR,
        C_OPTION_FNO_UNSIGNED_CHAR,
        C_OPTION_FUNSIGNED_CHAR,
        C_OPTION_FNO_SIGNED_CHAR,
        C_OPTION_FSHORT_WCHAR,
        C_OPTION_FNO_SHORT_WCHAR,
        C_OPTION_FGNU89_INLINE,
        C_OPTION_FNO_GNU89_INLINE,
        C_OPTION_FPIC_BIG,
        C_OPTION_FPIC_SMALL,
        C_OPTION_FPIE_BIG,
        C_OPTION_FPIE_SMALL,
        C_OPTION_FNO_PIC_BIG,
        C_OPTION_FNO_PIC_SMALL,
        C_OPTION_FNO_PIE_BIG,
        C_OPTION_FNO_PIE_SMALL,
        C_OPTION_FASYNCHRONOUS_UNWIND_TABLES,
        C_OPTION_FNO_ASYNCHRONOUS_UNWIND_TABLES,
        C_OPTION_FUNWIND_TABLES,
        C_OPTION_FNO_UNWIND_TABLES,
        // The frame pointer on request (#177, predefines_frame_pointer): both tiers' frames take it.
        C_OPTION_FOMIT_FRAME_POINTER,
        C_OPTION_FNO_OMIT_FRAME_POINTER,
        C_OPTION_MOMIT_LEAF_FRAME_POINTER,
        C_OPTION_MNO_OMIT_LEAF_FRAME_POINTER,
        // Stack-clash protection on request (#177, predefines_stack_clash): both tiers' frames and allocations probe.
        C_OPTION_FSTACK_CLASH_PROTECTION,
        C_OPTION_FNO_STACK_CLASH_PROTECTION,
        C_OPTION_FEXCEPTIONS,
        C_OPTION_FNO_EXCEPTIONS,
        C_OPTION_FCOMMON,
        C_OPTION_FNO_COMMON,
        C_OPTION_FOPENMP,
        C_OPTION_FNO_OPENMP,
        C_OPTION_FSANITIZE_CFI_CROSS_DSO,
        C_OPTION_FNO_SANITIZE_CFI_CROSS_DSO,
        C_OPTION_FDEFINE_TARGET_OS_MACROS,
        C_OPTION_FNO_DEFINE_TARGET_OS_MACROS,
        C_OPTION_MSSE4,
        C_OPTION_MNO_SSE4,
        C_OPTION_M80387,
        C_OPTION_MNO_80387,
        C_OPTION_MAPXF,
        C_OPTION_MNO_APXF,
        C_OPTION_MGENERAL_REGS_ONLY,
        C_OPTION_MLONG_DOUBLE_64,
        C_OPTION_MLONG_DOUBLE_80,
        C_OPTION_MLONG_DOUBLE_128,
    };
    // These grammars have explicit value parsers. In particular, coverage's '=' does NOT admit '-coverage-*', and
    // accepting a validated target feature does not admit the preprocessor's macro-neutral machine options.
    static const uint16_t values[] = {
        C_OPTION_FERROR_LIMIT_EQ,
        C_OPTION_FGNUC_VERSION_EQ,
        C_OPTION_FEXEC_CHARSET_EQ,
        C_OPTION_FINPUT_CHARSET_EQ,
        C_OPTION_FFP_CONTRACT_EQ,
        C_OPTION_FFP_EVAL_METHOD_EQ,
        C_OPTION_FSANITIZE_EQ,
        C_OPTION_FNO_SANITIZE_EQ,
        C_OPTION_FSANITIZE_IGNORE_FOR_UBSAN_FEATURE_EQ,
        C_OPTION_FSANITIZE_COVERAGE_EQ,
        C_OPTION_FNO_SANITIZE_COVERAGE_EQ,
        C_OPTION_FOPENMP_EQ,
        C_OPTION_FOPENMP_VERSION_EQ,
        C_OPTION_MARCH_EQ,
        C_OPTION_MTUNE_EQ,
        C_OPTION_MABI_EQ,
        C_OPTION_MAPX_FEATURES_EQ,
        C_OPTION_MNO_APX_FEATURES_EQ,
        // patchable_function_entry's NOPs for every function (#1447, predefines_patchable): both tiers' code takes them.
        C_OPTION_FPATCHABLE_FUNCTION_ENTRY_EQ,
    };
    // Exact compatibility controls: existing inactive/default behavior, permissions not obligations to optimize,
    // and presentation or C-inapplicable controls. No generic -fno-/-mno- or diagnostics-family escape hatch.
    // C exceptions above are not merely a no-op: they request the implemented unwind tables.
    static const uint16_t compatible[] = {
        C_OPTION_FTRIVIAL_AUTO_VAR_INIT_EQ_UNINITIALIZED,
        C_OPTION_FZERO_CALL_USED_REGS_EQ_SKIP,
        C_OPTION_MCMODEL_EQ_SMALL,
        C_OPTION_FNO_SPLIT_STACK,
        C_OPTION_FNO_TRAPV,
        C_OPTION_FNO_STACK_CHECK,
        C_OPTION_FNO_INSTRUMENT_FUNCTIONS,
        C_OPTION_FNO_PROFILE_ARCS,
        C_OPTION_FNO_TEST_COVERAGE,
        C_OPTION_FNO_PROFILE_GENERATE,
        C_OPTION_FNO_PROFILE_INSTR_GENERATE,
        C_OPTION_FNO_LTO,
        C_OPTION_FNO_BLOCKS,
        C_OPTION_FPLT,
        C_OPTION_FWRAPV,
        C_OPTION_FNO_WRAPV,
        C_OPTION_FSTRICT_OVERFLOW,
        C_OPTION_FNO_STRICT_OVERFLOW,
        C_OPTION_FSTRICT_ALIASING,
        C_OPTION_FNO_STRICT_ALIASING,
        C_OPTION_FNO_SEMANTIC_INTERPOSITION,
        C_OPTION_FRTTI,
        C_OPTION_FNO_RTTI,
        C_OPTION_FDWARF2_CFI_ASM,
        C_OPTION_FNO_DWARF2_CFI_ASM,
        C_OPTION_FDIAGNOSTICS_COLOR,
        C_OPTION_FNO_DIAGNOSTICS_COLOR,
        C_OPTION_FDIAGNOSTICS_COLOR_EQ_ALWAYS,
        C_OPTION_FDIAGNOSTICS_COLOR_EQ_NEVER,
        C_OPTION_FDIAGNOSTICS_COLOR_EQ_AUTO,
        C_OPTION_FCOLOR_DIAGNOSTICS,
        C_OPTION_FNO_COLOR_DIAGNOSTICS,
        C_OPTION_MFPMATH_EQ_SSE,
        C_OPTION_M64,
        C_OPTION_MRED_ZONE,
        C_OPTION_MNO_MS_BITFIELDS,
        C_OPTION_MNO_RTD,
        C_OPTION_MNO_IAMCU,
    };
    bool known = predefines_unsupported(argument, match);
    // Reuse the frontend's family inventory, including its last-option-wins refusals, instead of shadowing it with
    // another copy. An unsupported enabled family still fails in pp_settle_families, but a later disable may win.
    for (uint32_t i = 0; i < PP_FAMILY_COUNT; i += 1)
    {
        known |= match.exact == pp_families[i].on || (pp_families[i].off != C_OPTION_NONE && match.exact == pp_families[i].off);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(handled); i += 1)
    {
        known |= handled[i] == match.exact;
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(values); i += 1)
    {
        known |= values[i] == match.joined;
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(compatible); i += 1)
    {
        known |= compatible[i] == match.exact;
    }
    // A function name is an open namespace, not an option family. The preprocessor records exactly that name.
    known |= c_option_starts(argument, "-fno-builtin-") && argument[13] != 0;
    if (!known && argument[0] == '-' && argument[1] == 'm')
    {
        const char* name = c_option_starts(argument, "-mno-") ? argument + 5 : argument + 2;
        const int32_t feature = predefines_feature_index(name, strlen(name));
        known = feature >= 0 && predefines_features[feature].flag == PREDEFINES_FLAG_PLAIN;
    }
    return known;
}
