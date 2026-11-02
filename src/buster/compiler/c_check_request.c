// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// One C compilation request's semantic configuration (#1057): what every client that prepares real source as the C
// driver does takes from the same options, from one place, so that a benchmark or a test that reads the driver's options
// can't compile another target, ABI or language than -c compiles for them. Its clients are the driver
// (c_checker_driver.c), the corpus check (c_check_corpus.c) and the real-source preparation that the source benchmarks
// and the IR's commands on real C share (c_check_source_prepare.c).
//
// - check_request_scan reads the command line's code generation and ABI layout options that no PPOptions field decides,
//   as the driver's arguments' loop reads them, each family's deciding option as clang's driver takes it, and says
//   which of them go on to pp_parse_options;
// - check_request_refusal says whether the request asks for what the driver doesn't implement, and which option asked
//   for it, and check_request_describe writes the driver's message for it: one refusal for every client;
// - check_request_read and check_request_parse are the clients' reading of their options and file: scanned, parsed,
//   refused or accepted whole, so that no client goes on with a partly read configuration;
// - check_request_program_options makes a checked program's options from the unit's preprocessor, whose options are the
//   request's own copy: the generated code's target and the predefines its functions' target attributes resolve
//   against, never a client's own copy that could miss them;
// - check_request_receipt describes the resolved configuration for a measurement's report, on request: a stable digest
//   and the resolved target, language, -c's own output requirements and the client's explicit overrides. Nothing
//   computes it on an ordinary compile.
//
// The configuration lives as long as what it borrows: a PPOptions' strings and arrays are the command line's and its
// option arena's (pp_parse_options), and a checked program's predefines are its preprocessor's (pp->options), which
// outlives the program's checking and selection. A command of several units, or several commands in one process, read
// their options again; nothing here keeps state between requests.
#ifndef BUSTER_C_CHECK_REQUEST_INCLUDED
#define BUSTER_C_CHECK_REQUEST_INCLUDED

// The receipt's text's room, with its terminator.
#define BUSTER_CHECK_REQUEST_RECEIPT_BYTES 512

// The command line's code generation and ABI layout options that no PPOptions field decides, as the driver reads them
// (check_request_scan): each family's option that decides, or null for none. Zero is a command line without any.
typedef struct CheckRequestScan CheckRequestScan;
struct CheckRequestScan
{
    // The first option that asks for code generation the driver doesn't implement (check_request_codegen_option).
    const char* codegen;
    // The last of -fstack-protector, -fstack-protector-strong and -fstack-protector-all, unless -fno-stack-protector
    // follows it.
    const char* stack_protector;
    // The last -fcf-protection or -fcf-protection=VALUE, unless it is -fcf-protection=none.
    const char* cf_protection;
    // The last -fshort-enums, unless -fno-short-enums follows it.
    const char* short_enums;
    // Clang's packing: the last -fpack-struct=N decides when there is one (`pack_value`), packing when N isn't 0
    // (`packed_value`, that option) and invalid when N isn't a number (`invalid_pack`); without one, the last bare
    // -f[no-]pack-struct does (`packed_bare`, -fpack-struct's).
    const char* packed_bare;
    const char* packed_value;
    const char* invalid_pack;
    bool pack_value;
};

// What a request asks for that the driver doesn't implement (check_request_refusal), in the order the driver reports
// them: the first that applies.
typedef enum CheckRequestError
{
    CHECK_REQUEST_ACCEPTED,
    // Code generation the driver doesn't implement: the first option of check_request_codegen_option's, else the stack
    // protector's or -fcf-protection's that decides.
    CHECK_REQUEST_CODEGEN,
    // Position-independent code for shared libraries (-fPIC, -fpic), for a request that emits code: Tier 0 can't yet
    // emit shared-library-safe symbol and TLS accesses (#811).
    CHECK_REQUEST_PIC,
    // An ABI layout the checker doesn't implement (-fshort-enums, -fpack-struct).
    CHECK_REQUEST_LAYOUT,
    // A language or target the checker doesn't take (check_input_target_supported).
    CHECK_REQUEST_TARGET,
} CheckRequestError;

typedef struct CheckRequestRefusal CheckRequestRefusal;
struct CheckRequestRefusal
{
    CheckRequestError error;
    // The option that asked for it (the command line's own string), or null for CHECK_REQUEST_TARGET.
    const char* option;
};

// A resolved configuration's description for a measurement's report (check_request_receipt).
typedef struct CheckRequestReceipt CheckRequestReceipt;
struct CheckRequestReceipt
{
    // The configuration's identity: a digest of the resolved target, the language and its lexical options, the
    // predefined macros' text the unit was preprocessed with, -D, -U and -include, the header search, -c's own output
    // requirements and the client's overrides; never a pointer's value or an intern id.
    uint64_t digest;
    // "configuration=DIGEST target=NAME features=HEX extensions=HEX language=STD unwind=on|off debug=on|off
    // overrides=TEXT", without a newline.
    char text[BUSTER_CHECK_REQUEST_RECEIPT_BYTES];
};

// The options that require code generation or ABI behavior the C driver doesn't yet implement. The preprocessor may
// still take them to model predefined macros; a semantic-driver command must not silently emit an unprotected or
// differently instrumented object. Negative and default forms that don't request those behaviors are left alone.
static bool check_request_codegen_option(const char* argument)
{
    static const char* const exact[] = {
        "-fstack-protector-explicit", "-fstack-clash-protection", "-fstack-check", "-pg", "-finstrument-functions",
        "-finstrument-functions-after-inlining", "-fprofile-generate", "-fprofile-instr-generate", "-fcs-profile-generate",
        "-fprofile-generate-cold-function-coverage", "--coverage", "-fprofile-arcs", "-ftest-coverage", "-mno-red-zone", "-fno-plt",
        "-fpcc-struct-return", "-freg-struct-return", "-fshort-double",
    };
    static const char* const prefixes[] = { "-fstack-protector", "-fstack-check=", "-ftls-model=", "-fwide-exec-charset=",
                                            "-fprofile-generate=", "-fprofile-instr-generate=", "-fcs-profile-generate=",
                                            "-fprofile-generate-cold-function-coverage=" };
    bool unsupported = false;
    for (uint32_t i = 0; !unsupported && i < BUSTER_ARRAY_LENGTH(exact); i += 1)
    {
        unsupported = strcmp(argument, exact[i]) == 0;
    }
    for (uint32_t i = 0; !unsupported && i < BUSTER_ARRAY_LENGTH(prefixes); i += 1)
    {
        unsupported = strncmp(argument, prefixes[i], strlen(prefixes[i])) == 0;
    }
    return unsupported;
}

// -fpack-struct=N's N, as clang reads it (a C integer literal's prefixes, 32 bits): whether it is one, its value in
// *value.
static bool check_request_pack_value(const char* digits, uint64_t* value)
{
    uint32_t base = 10;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X' || digits[1] == 'b' || digits[1] == 'B'))
    {
        base = digits[1] == 'x' || digits[1] == 'X' ? 16 : 2;
        digits += 2;
    }
    else if (digits[0] == '0')
    {
        base = 8;
    }
    bool valid = *digits != 0;
    uint64_t result = 0;
    for (const char* digit = digits; *digit != 0; digit += 1)
    {
        const uint32_t number = *digit >= '0' && *digit <= '9' ? (uint32_t)(*digit - '0') :
                                *digit >= 'a' && *digit <= 'f' ? (uint32_t)(*digit - 'a' + 10) :
                                *digit >= 'A' && *digit <= 'F' ? (uint32_t)(*digit - 'A' + 10) : UINT32_MAX;
        valid &= number < base && result <= (UINT32_MAX - (uint64_t)number) / base;
        result = valid ? result * base + number : 0;
    }
    *value = result;
    return valid;
}

// Reads `argument`, one that is no option's value (pp_option_takes_value's), into `scan`: whether it goes on to
// pp_parse_options. Only an option that asks for code generation the driver doesn't implement stays apart, since the
// preprocessor doesn't model it; the stack protector's, -fcf-protection's and the layout options go on, for their
// predefined macros.
static bool check_request_scan(CheckRequestScan* scan, const char* argument)
{
    bool passed = true;
    if (strcmp(argument, "-fshort-enums") == 0 || strcmp(argument, "-fno-short-enums") == 0)
    {
        scan->short_enums = strcmp(argument, "-fshort-enums") == 0 ? argument : NULL;
    }
    else if (strcmp(argument, "-fpack-struct") == 0 || strcmp(argument, "-fno-pack-struct") == 0)
    {
        scan->packed_bare = strcmp(argument, "-fpack-struct") == 0 ? argument : NULL;
    }
    else if (strncmp(argument, "-fpack-struct=", sizeof("-fpack-struct=") - 1) == 0)
    {
        uint64_t value = 0;
        const bool valid = check_request_pack_value(argument + sizeof("-fpack-struct=") - 1, &value);
        scan->pack_value = true;
        scan->invalid_pack = valid ? NULL : argument;
        scan->packed_value = value == 0 && valid ? NULL : argument;
    }
    else if (strcmp(argument, "-fno-stack-protector") == 0 || strcmp(argument, "-fstack-protector") == 0 || strcmp(argument, "-fstack-protector-strong") == 0 ||
             strcmp(argument, "-fstack-protector-all") == 0)
    {
        scan->stack_protector = strcmp(argument, "-fno-stack-protector") == 0 ? NULL : argument;
    }
    else if (strcmp(argument, "-fcf-protection") == 0 || strncmp(argument, "-fcf-protection=", sizeof("-fcf-protection=") - 1) == 0)
    {
        scan->cf_protection = strcmp(argument, "-fcf-protection=none") == 0 ? NULL : argument;
    }
    else if (check_request_codegen_option(argument))
    {
        scan->codegen = scan->codegen != NULL ? scan->codegen : argument;
        passed = false;
    }
    return passed;
}

// What the request scanned into `scan` asks for that the driver doesn't implement: with `options` (null before they
// are parsed: then only code generation's options, which the driver reports before a usage error), its position-
// independent code when `emits` (-c, or a link), its layout and its language and target too, the first that applies in
// that order.
static CheckRequestRefusal check_request_refusal(const CheckRequestScan* scan, const PPOptions* options, bool emits)
{
    CheckRequestRefusal result = { .error = CHECK_REQUEST_ACCEPTED };
    const char* codegen = scan->codegen != NULL ? scan->codegen : scan->stack_protector != NULL ? scan->stack_protector : scan->cf_protection;
    const char* pic = options != NULL && emits ? (options->predefines.pic == BUSTER_PREDEFINES_PIC_BIG ? "-fPIC" :
                                                  options->predefines.pic == BUSTER_PREDEFINES_PIC_SMALL ? "-fpic" : NULL) : NULL;
    const char* packed = scan->pack_value ? scan->packed_value : scan->packed_bare;
    const char* layout = scan->invalid_pack != NULL ? scan->invalid_pack : scan->short_enums != NULL ? scan->short_enums : packed;
    if (codegen != NULL)
    {
        result = (CheckRequestRefusal){ .error = CHECK_REQUEST_CODEGEN, .option = codegen };
    }
    else if (pic != NULL)
    {
        result = (CheckRequestRefusal){ .error = CHECK_REQUEST_PIC, .option = pic };
    }
    else if (options != NULL && layout != NULL)
    {
        result = (CheckRequestRefusal){ .error = CHECK_REQUEST_LAYOUT, .option = layout };
    }
    else if (options != NULL && !check_input_target_supported(options))
    {
        result = (CheckRequestRefusal){ .error = CHECK_REQUEST_TARGET };
    }
    return result;
}

// Writes the driver's message for `refusal` (after "error: ", without a newline) to `stream`; nothing for an accepted
// request. The option is written whole, however long its value.
static void check_request_describe(FILE* stream, CheckRequestRefusal refusal)
{
    switch (refusal.error)
    {
        case CHECK_REQUEST_CODEGEN:
            fprintf(stream, "unsupported code generation option '%s'", refusal.option);
            break;
        case CHECK_REQUEST_PIC:
            fprintf(stream, "unsupported code generation option '%s' (position-independent code for shared libraries is not implemented)", refusal.option);
            break;
        case CHECK_REQUEST_LAYOUT:
            fprintf(stream, "unsupported option '%s' (the checker does not implement its ABI layout)", refusal.option);
            break;
        case CHECK_REQUEST_TARGET:
            fprintf(stream, "checker requires C, 32-bit wchar_t, x87 long double and source-precision floating evaluation");
            break;
        case CHECK_REQUEST_ACCEPTED:
            break;
    }
}

// check_request_describe's message in `text`, `size` bytes with its terminator, cut short past them: a client's
// report's field. Nothing is written for a size of 0.
static void check_request_describe_text(CheckRequestRefusal refusal, char* text, size_t size)
{
    if (size != 0)
    {
        memset(text, 0, size);
        FILE* stream = size > 1 ? fmemopen(text, size - 1, "w") : NULL;
        if (stream != NULL)
        {
            check_request_describe(stream, refusal);
            fclose(stream);
        }
        text[size - 1] = 0;
    }
}

// The request of a client that reads -E's options and its file, `arguments` (`count` of them, the client's own options
// taken out), as the driver reads them (the source benchmarks', the IR's commands' on real C): the arguments scanned
// (check_request_scan), those that go on and the file parsed by pp_parse_options in `arena`, which the result's strings
// and arrays borrow from, then refused or accepted as the driver would (check_request_refusal, with code generation's
// refusals when `emits`). Whether the request was read and accepted, with it in *parsed (semantic pragmas on, as every
// semantic consumer reads them) and the file's path in *input (null if the arguments name none); otherwise *parsed is
// all zero, never a partly read configuration, *input is null, and *refusal says why, if the options were read and
// refused, or is CHECK_REQUEST_ACCEPTED where they couldn't be read at all (no room, an option pp_parse_options refuses
// or one without its value, or -o).
static bool check_request_read(PPOptions* parsed, CheckRequestRefusal* refusal, const char** input, char** arguments, uint32_t count, bool emits, Arena* arena)
{
    char** own = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)count + 1);
    CheckRequestScan scan = { 0 };
    uint32_t passed = 0;
    for (uint32_t i = 0; own != 0 && i < count; i += 1)
    {
        const bool valued = i + 1 < count && pp_option_takes_value(arguments[i]);
        if (valued)
        {
            own[passed++] = arguments[i];
            own[passed++] = arguments[i + 1];
            i += 1;
        }
        else if (check_request_scan(&scan, arguments[i]))
        {
            own[passed++] = arguments[i];
        }
    }
    PPOptions result = { 0 };
    const char* path = 0;
    const char* output = 0;
    bool markers = false;
    const CheckRequestRefusal codegen = check_request_refusal(&scan, NULL, emits);
    const bool read = own != 0 && codegen.error == CHECK_REQUEST_ACCEPTED && pp_parse_options((int)passed, own, &result, arena, &path, &output, &markers) &&
                      output == 0;
    *refusal = read ? check_request_refusal(&scan, &result, emits) : codegen;
    const bool accepted = read && refusal->error == CHECK_REQUEST_ACCEPTED;
    result.semantic_pragmas = true;
    if (accepted)
    {
        *parsed = result;
    }
    else
    {
        memset(parsed, 0, sizeof(*parsed));
    }
    *input = accepted ? path : 0;
    return accepted;
}

// check_request_read of `file` read with `options` (`option_count` of them, -E's, before it): accepted only with the
// file as its input, so that an option without its value, which would take the file's place, leaves no configuration.
static bool check_request_parse(PPOptions* parsed, CheckRequestRefusal* refusal, const char** input, const char* file, char** options, uint32_t option_count,
                                bool emits, Arena* arena)
{
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)option_count + 1);
    for (uint32_t i = 0; arguments != 0 && i < option_count; i += 1)
    {
        arguments[i] = options[i];
    }
    if (arguments != 0)
    {
        arguments[option_count] = (char*)file;
    }
    bool accepted = arguments != 0 && check_request_read(parsed, refusal, input, arguments, option_count + 1, emits, arena);
    if (arguments == 0)
    {
        *refusal = (CheckRequestRefusal){ .error = CHECK_REQUEST_ACCEPTED };
        memset(parsed, 0, sizeof(*parsed));
        *input = 0;
    }
    else if (accepted && *input == 0)
    {
        accepted = false;
        memset(parsed, 0, sizeof(*parsed));
    }
    return accepted;
}

// The checked program's options for a unit that `pp` preprocessed, from the request's own copy of the options that
// preprocessed it (pp->options): the identifiers its names are interned in, the generated code's target and the
// predefines its functions' target attributes resolve against (CheckProgram.body_targets), the same for every client,
// so that none compiles for another target than its source was preprocessed for. `unit` holds the unit's own borrowed
// state (its initializers, assembly, prepared data and their references) and the client's explicit overrides (the
// scalar path, #129's models, #177's lifetimes), which this keeps. The predefines are `pp`'s: the program reads them
// while it is checked, so `pp` outlives check_program.
static CheckProgramOptions check_request_program_options(const Preprocessor* pp, CheckProgramOptions unit)
{
    unit.identifiers = pp->table;
    unit.target = pp->options.target;
    unit.predefines = &pp->options.predefines;
    return unit;
}

// The receipt's digest with `length` bytes at `bytes` mixed in after field `tag`.
static uint64_t check_request_mix(uint64_t digest, uint32_t tag, const void* bytes, uint64_t length)
{
    const uint64_t words[3] = { digest, tag, link_hash((const uint8_t*)bytes, length) };
    return link_hash((const uint8_t*)words, sizeof(words));
}

// The digest with a word mixed in after `tag`.
static uint64_t check_request_mix_word(uint64_t digest, uint32_t tag, uint64_t word)
{
    return check_request_mix(digest, tag, &word, sizeof(word));
}

// The digest with a list of strings mixed in after `tag`, each with its length, and their count.
static uint64_t check_request_mix_strings(uint64_t digest, uint32_t tag, const char* const* strings, uint32_t count)
{
    uint64_t result = check_request_mix_word(digest, tag, count);
    for (uint32_t i = 0; i < count; i += 1)
    {
        result = check_request_mix(result, tag, strings[i], strlen(strings[i]));
    }
    return result;
}

// The configuration that `pp` preprocessed its unit with, described for a measurement's report: the digest of
// everything that decides what the unit's code is (CheckRequestReceipt.digest) and its text. -c's own output
// requirements for the request (its unwind tables, predefines_unwind_tables, and -g's rows) are part of it, and
// `overrides` (a client's own text, null for none) says what the client does otherwise: its path, selection, lanes,
// emitted tables. Read after pp_run, whose predefined macros' text it hashes; nothing else calls it, so an ordinary
// compile pays nothing for it.
static CheckRequestReceipt check_request_receipt(const Preprocessor* pp, const char* overrides)
{
    const PPOptions* options = &pp->options;
    const PredefinesOptions* predefines = &options->predefines;
    const X86Target target = options->target;
    const CLanguage language = options->language;
    const bool unwind = predefines_unwind_tables(predefines, options->freestanding);
    const bool debug = predefines->debug || predefines->debug_modules;
    const char* override_text = overrides != NULL ? overrides : "none";
    uint64_t digest = check_request_mix_word(0, 1, target.features);
    digest = check_request_mix_word(digest, 2, target.extensions);
    digest = check_request_mix_word(digest, 3, (uint64_t)target.architecture << 1 | (target.native_znver5 ? 1u : 0u));
    digest = check_request_mix_word(digest, 4, (uint64_t)language.standard << 32 | (uint64_t)language.gnu << 16 | (uint64_t)language.gnu_keywords << 8 | language.digraphs);
    const uint64_t lexical = (uint64_t)options->freestanding | (uint64_t)options->no_builtin << 1 | (uint64_t)options->undefine << 2 | (uint64_t)options->trigraphs << 3 |
                             (uint64_t)options->assembly << 4 | (uint64_t)options->clang_features << 5 | (uint64_t)options->no_standard_includes << 6 |
                             (uint64_t)options->no_builtin_includes << 7 | (uint64_t)options->buster_headers << 8 | (uint64_t)options->implicit_function_system << 9 |
                             (uint64_t)options->dollars << 16 | (uint64_t)options->raw_strings << 24 | (uint64_t)options->implicit_function_diagnostic << 32 |
                             (uint64_t)options->implicit_library_diagnostic << 40;
    digest = check_request_mix_word(digest, 5, lexical);
    // The predefines' fields the checker and the backend read beside their macros, then the macros' text itself.
    const uint64_t semantic = (uint64_t)predefines->char_unsigned | (uint64_t)predefines->short_wchar << 1 | (uint64_t)predefines->long_double << 8 |
                              (uint64_t)predefines->fp_eval_method << 16 | (uint64_t)predefines->pic << 24 | (uint64_t)predefines->optimization << 32 |
                              (uint64_t)predefines->wrapv << 40 | (uint64_t)unwind << 48 | (uint64_t)debug << 49;
    digest = check_request_mix_word(digest, 6, semantic);
    const bool predefined = pp->predefines_data != BUSTER_PP_NONE;
    digest = predefined ? check_request_mix(digest, 7, pp->datas[pp->predefines_data].bytes, pp->datas[pp->predefines_data].length) : check_request_mix_word(digest, 7, 0);
    digest = check_request_mix_strings(digest, 8, options->no_builtin_functions, options->no_builtin_function_count);
    digest = check_request_mix_strings(digest, 9, options->macros, options->macro_count);
    digest = check_request_mix_strings(digest, 10, options->includes, options->include_count);
    digest = check_request_mix_strings(digest, 11, options->quote_directories, options->quote_directory_count);
    digest = check_request_mix_strings(digest, 12, options->include_directories, options->include_directory_count);
    digest = check_request_mix_strings(digest, 13, options->system_directories, options->system_directory_count);
    digest = check_request_mix_strings(digest, 14, options->after_directories, options->after_directory_count);
    digest = check_request_mix_strings(digest, 15, options->embed_directories, options->embed_directory_count);
    digest = check_request_mix(digest, 16, override_text, strlen(override_text));
    static const char* const architectures[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native", "other" };
    const uint32_t architecture = (uint32_t)target.architecture;
    CheckRequestReceipt result = { .digest = digest };
    snprintf(result.text, sizeof(result.text),
             "configuration=%016" PRIx64 " target=%s features=%016" PRIx64 " extensions=%016" PRIx64 " language=%s%02u unwind=%s debug=%s overrides=%s", digest,
             architecture < BUSTER_ARRAY_LENGTH(architectures) ? architectures[architecture] : "unknown", target.features, target.extensions,
             language.gnu ? "gnu" : "c", language.standard % 100, unwind ? "on" : "off", debug ? "on" : "off", override_text);
    return result;
}

#endif
