// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1057's tests: one request's configuration (c_check_request.c) and the shared source preparation
// (c_check_source_prepare.c) against the C driver, in this process and in children that run the driver's command.
//
// - refusals: option lists of each code generation, PIC, layout and target family, in the orders where the last or the
//   first decides, read by check_request_parse as a client reads them: the refusal and its option, the arguments that
//   go on to the preprocessor, an option's value never read as an option, and a refused or unreadable request (an
//   arena without room, an option without its value after pp_parse_options filled the fields of the options before it)
//   leaving its configuration all zero; then the driver on each list, by -c and by -fsyntax-only, whose exit status and first line of errors
//   must be check_request_describe's;
// - the program's options: check_request_program_options takes the identifiers, the target and the predefines from
//   the preprocessor and keeps the unit's own fields;
// - receipts: one file prepared twice gives the same digest and text; another -march, -D, -funsigned-char, -I or
//   override changes the digest, and only -march the target's text;
// - parity: original fixtures (scalar code with static data and a switch's jump table, 32-byte and 64-byte vectors, a
//   function's target attribute, predefined and command-line macros, an always_inline callee whose target the caller
//   lacks) prepared as the source clients prepare them and emitted, published and built as -c builds its object
//   (check_driver_object_build), against the driver's -c object of the same options, byte for byte, for the
//   baseline, AVX2 and AVX-512 targets, a function's target attribute, language and predefine switches and the scalar
//   path; or refused by both, at the same stage's kind: -c's emission refusal, its target-feature errors, or the
//   request's refusal. The parity cases run in an order that changes the target from one to the next, and the first
//   again last, whose object must be the first's: one preparation's configuration never reaches the next.
//
// Include after c_check_source_prepare.c; test-checker calls check_test_requests through its forward declaration.
#ifndef BUSTER_C_CHECK_REQUEST_TEST_INCLUDED
#define BUSTER_C_CHECK_REQUEST_TEST_INCLUDED

// The driver's command `arguments` (run_check_command's) in a child, its standard output to /dev/null and its errors
// into `errors`: its exit status, or -1 if it couldn't run.
static int check_request_test_driver(char** arguments, uint32_t count, const char* errors)
{
    int result = -1;
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        const int out = open("/dev/null", O_WRONLY);
        const int err = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out < 0 || err < 0 || dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0)
        {
            _exit(126);
        }
        close(out);
        close(err);
        const int code = run_check_command((int)count, arguments, true);
        fflush(NULL);
        _exit(code);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && errno == EINTR);
    if (waited == child && child > 0 && WIFEXITED(status))
    {
        result = WEXITSTATUS(status);
    }
    return result;
}

// The file at `path`, malloc'd (the caller's to free) with its length in *length, or null.
static uint8_t* check_request_test_read(const char* path, uint64_t* length)
{
    FILE* file = fopen(path, "rb");
    const long size = file != NULL && fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    uint8_t* bytes = size >= 0 && size < (1l << 28) && fseek(file, 0, SEEK_SET) == 0 ? malloc((size_t)size + 1) : NULL;
    const bool read = bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (file != NULL)
    {
        fclose(file);
    }
    if (!read)
    {
        free(bytes);
        bytes = NULL;
    }
    *length = read ? (uint64_t)size : 0;
    return bytes;
}

// Whether `bytes` are all zero.
static bool check_request_test_zero(const void* bytes, uint64_t length)
{
    const uint8_t* byte = bytes;
    bool zero = true;
    for (uint64_t i = 0; zero && i < length; i += 1)
    {
        zero = byte[i] == 0;
    }
    return zero;
}

typedef struct CheckRequestTestRefusal CheckRequestTestRefusal;
struct CheckRequestTestRefusal
{
    const char* label;
    // The options before the file, ended by null.
    const char* options[5];
    // Whether the client emits code, as -c does.
    bool emits;
    CheckRequestError error;
    // The option the refusal names, or null.
    const char* option;
    // How many of the options go on to the preprocessor.
    uint32_t passed;
};

static void check_test_request_refusals(CheckTest* test, const char* directory, const char* file)
{
    static const CheckRequestTestRefusal cases[] = {
        { .label = "no option", .options = { 0 }, .emits = true, .error = CHECK_REQUEST_ACCEPTED },
        { .label = "-pg, which the preprocessor doesn't read", .options = { "-pg", 0 }, .emits = true, .error = CHECK_REQUEST_CODEGEN, .option = "-pg" },
        { .label = "the first code generation option", .options = { "--coverage", "-pg", 0 }, .emits = true, .error = CHECK_REQUEST_CODEGEN, .option = "--coverage" },
        { .label = "a stack protector", .options = { "-fstack-protector-strong", 0 }, .emits = false, .error = CHECK_REQUEST_CODEGEN,
          .option = "-fstack-protector-strong", .passed = 1 },
        { .label = "a stack protector turned off again", .options = { "-fstack-protector-all", "-fno-stack-protector", 0 }, .emits = true,
          .error = CHECK_REQUEST_ACCEPTED, .passed = 2 },
        { .label = "-fstack-protector-explicit, a code generation option", .options = { "-fstack-protector-explicit", 0 }, .emits = true,
          .error = CHECK_REQUEST_CODEGEN, .option = "-fstack-protector-explicit" },
        { .label = "a code generation option before the stack protector's", .options = { "-fstack-protector", "-finstrument-functions", 0 }, .emits = true,
          .error = CHECK_REQUEST_CODEGEN, .option = "-finstrument-functions", .passed = 1 },
        { .label = "-fcf-protection", .options = { "-fcf-protection=full", 0 }, .emits = true, .error = CHECK_REQUEST_CODEGEN, .option = "-fcf-protection=full",
          .passed = 1 },
        { .label = "-fcf-protection=none last", .options = { "-fcf-protection", "-fcf-protection=none", 0 }, .emits = true, .error = CHECK_REQUEST_ACCEPTED,
          .passed = 2 },
        { .label = "-fPIC for code", .options = { "-fPIC", 0 }, .emits = true, .error = CHECK_REQUEST_PIC, .option = "-fPIC", .passed = 1 },
        { .label = "-fpic for code", .options = { "-fpic", 0 }, .emits = true, .error = CHECK_REQUEST_PIC, .option = "-fpic", .passed = 1 },
        { .label = "-fPIC without code", .options = { "-fPIC", 0 }, .emits = false, .error = CHECK_REQUEST_ACCEPTED, .passed = 1 },
        { .label = "-fPIC turned off again", .options = { "-fPIC", "-fno-pic", 0 }, .emits = true, .error = CHECK_REQUEST_ACCEPTED, .passed = 2 },
        { .label = "-fshort-enums", .options = { "-fshort-enums", 0 }, .emits = false, .error = CHECK_REQUEST_LAYOUT, .option = "-fshort-enums", .passed = 1 },
        { .label = "-fshort-enums turned off again", .options = { "-fshort-enums", "-fno-short-enums", 0 }, .emits = true, .error = CHECK_REQUEST_ACCEPTED,
          .passed = 2 },
        { .label = "a bare -fpack-struct", .options = { "-fpack-struct", 0 }, .emits = true, .error = CHECK_REQUEST_LAYOUT, .option = "-fpack-struct", .passed = 1 },
        { .label = "-fpack-struct=0 after a bare one", .options = { "-fpack-struct", "-fpack-struct=0", 0 }, .emits = true, .error = CHECK_REQUEST_ACCEPTED,
          .passed = 2 },
        { .label = "-fpack-struct=0x8", .options = { "-fpack-struct=0x8", 0 }, .emits = true, .error = CHECK_REQUEST_LAYOUT, .option = "-fpack-struct=0x8",
          .passed = 1 },
        { .label = "an invalid -fpack-struct= value", .options = { "-fpack-struct=08", 0 }, .emits = true, .error = CHECK_REQUEST_LAYOUT,
          .option = "-fpack-struct=08", .passed = 1 },
        { .label = "-fshort-wchar", .options = { "-fshort-wchar", 0 }, .emits = false, .error = CHECK_REQUEST_TARGET, .passed = 1 },
        { .label = "PIC before a layout", .options = { "-fshort-enums", "-fPIC", 0 }, .emits = true, .error = CHECK_REQUEST_PIC, .option = "-fPIC", .passed = 2 },
        { .label = "a layout before a target", .options = { "-fshort-wchar", "-fshort-enums", 0 }, .emits = true, .error = CHECK_REQUEST_LAYOUT,
          .option = "-fshort-enums", .passed = 2 },
        { .label = "an option's value isn't an option", .options = { "-I", "-pg", 0 }, .emits = true, .error = CHECK_REQUEST_ACCEPTED, .passed = 2 },
    };
    char errors_path[PATH_MAX];
    char object_path[PATH_MAX];
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", directory);
    snprintf(object_path, sizeof(object_path), "%s/refusal.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const CheckRequestTestRefusal* item = cases + c;
        char* options[8];
        uint32_t count = 0;
        while (item->options[count] != 0)
        {
            options[count] = (char*)item->options[count];
            count += 1;
        }
        // The arguments that go on to the preprocessor: those check_request_scan passes, and an option's value.
        CheckRequestScan scan = { 0 };
        uint32_t passed = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const bool valued = i + 1 < count && pp_option_takes_value(options[i]);
            passed += valued ? 2 : check_request_scan(&scan, options[i]) ? 1 : 0;
            i += valued ? 1 : 0;
        }
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        PPOptions parsed;
        memset(&parsed, 0xa5, sizeof(parsed));
        CheckRequestRefusal refusal = { 0 };
        const char* input = "poisoned";
        const bool accepted = arena.base != 0 && check_request_parse(&parsed, &refusal, &input, file, options, count, item->emits, &arena);
        const bool option = item->option == 0 ? refusal.option == 0 : refusal.option != 0 && strcmp(refusal.option, item->option) == 0;
        const bool zero = accepted ? parsed.semantic_pragmas && input == file : check_request_test_zero(&parsed, sizeof(parsed)) && input == 0;
        char label[256];
        snprintf(label, sizeof(label), "request: %s: %s, %u of its options for the preprocessor", item->label,
                 item->error == CHECK_REQUEST_ACCEPTED ? "accepted" : "refused", item->passed);
        check_test_expect(test, accepted == (item->error == CHECK_REQUEST_ACCEPTED) && refusal.error == item->error && option && zero && passed == item->passed,
                          label);
        arena_release(&arena);
        // The driver on the same options: -c for a client that emits, -fsyntax-only otherwise.
        char* arguments[16];
        uint32_t argument_count = 0;
        arguments[argument_count++] = item->emits ? "-c" : "-fsyntax-only";
        if (item->emits)
        {
            arguments[argument_count++] = "-o";
            arguments[argument_count++] = object_path;
        }
        arguments[argument_count++] = "-nostdinc";
        for (uint32_t i = 0; i < count; i += 1)
        {
            arguments[argument_count++] = options[i];
        }
        arguments[argument_count++] = (char*)file;
        const int status = check_request_test_driver(arguments, argument_count, errors_path);
        uint64_t length = 0;
        uint8_t* text = check_request_test_read(errors_path, &length);
        char expected[512] = { 0 };
        FILE* stream = fmemopen(expected, sizeof(expected) - 1, "w");
        if (stream != NULL)
        {
            if (item->error != CHECK_REQUEST_ACCEPTED)
            {
                fprintf(stream, "error: ");
                check_request_describe(stream, refusal);
                fprintf(stream, "\n");
            }
            fclose(stream);
        }
        const bool driven = text != NULL && status == (item->error == CHECK_REQUEST_ACCEPTED ? 0 : 1) && length == strlen(expected) &&
                            memcmp(text, expected, length) == 0;
        snprintf(label, sizeof(label), "request: %s: the driver's %s %s it with the same message", item->label, item->emits ? "-c" : "-fsyntax-only",
                 item->error == CHECK_REQUEST_ACCEPTED ? "accepts" : "refuses");
        check_test_expect(test, driven, label);
        if (!driven)
        {
            printf("request case \"%s\": status=%d expected=\"%s\" errors:\n%.*s\n", item->label, status, expected, text != NULL ? (int)length : 0,
                   text != NULL ? (const char*)text : "");
        }
        free(text);
    }
    // A request that can't be read leaves no configuration: no room for its arguments, and an option without its value
    // (-include takes the file's place, and leaves no input) after pp_parse_options filled the fields of the options
    // before it.
    PPOptions parsed;
    memset(&parsed, 0xa5, sizeof(parsed));
    CheckRequestRefusal refusal = { .error = CHECK_REQUEST_LAYOUT };
    const char* input = "poisoned";
    Arena empty = { 0 };
    char* plain[] = { "-DVALUE=1" };
    bool read = check_request_parse(&parsed, &refusal, &input, file, plain, 1, true, &empty);
    check_test_expect(test, !read && refusal.error == CHECK_REQUEST_ACCEPTED && check_request_test_zero(&parsed, sizeof(parsed)) && input == 0,
                      "request: no room for its arguments leaves a zero configuration, unrefused");
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    char* malformed[] = { "-I", "include", "-DVALUE=1", "-include" };
    memset(&parsed, 0xa5, sizeof(parsed));
    input = "poisoned";
    read = arena.base != 0 && check_request_parse(&parsed, &refusal, &input, file, malformed, BUSTER_ARRAY_LENGTH(malformed), true, &arena);
    check_test_expect(test, !read && refusal.error == CHECK_REQUEST_ACCEPTED && check_request_test_zero(&parsed, sizeof(parsed)) && input == 0,
                      "request: an option without its value leaves a zero configuration, though the options before it were read");
    arena_release(&arena);
    unlink(errors_path);
    unlink(object_path);
}

static void check_test_request_program_options(CheckTest* test)
{
    Preprocessor* pp = calloc(1, sizeof(Preprocessor));
    const bool ready = pp != NULL;
    check_test_expect(test, ready, "request: a preprocessor for the program's options");
    if (ready)
    {
        InternTable table = { 0 };
        pp->table = &table;
        pp->options.target = (X86Target){ .features = BUSTER_X86_FEATURE(AVX2), .extensions = 1, .architecture = X86_ARCH_V3 };
        const CheckInitializers initializers = { 0 };
        const uint32_t references[1] = { 1 };
        const CheckProgramOptions unit = {
            .initializers = &initializers,
            .initializer_references = references,
            .initializer_reference_count = 1,
            .scalar = true,
            .lifetimes = true,
            .folded_ifs = true,
            .target = { .features = UINT64_MAX },
            .identifiers = (const InternTable*)&initializers,
        };
        const CheckProgramOptions made = check_request_program_options(pp, unit);
        check_test_expect(test, made.identifiers == &table && made.target.features == BUSTER_X86_FEATURE(AVX2) && made.target.extensions == 1 &&
                                made.target.architecture == X86_ARCH_V3 && made.predefines == &pp->options.predefines,
                          "request: the program's identifiers, target and predefines are the preprocessor's");
        check_test_expect(test, made.initializers == &initializers && made.initializer_references == references && made.initializer_reference_count == 1 &&
                                made.scalar && made.lifetimes && made.folded_ifs && !made.unselected_operands,
                          "request: the program's options keep the unit's own fields and the client's overrides");
    }
    free(pp);
}

// The receipt of `file` prepared with `options` and `overrides` in *receipt: whether it was prepared.
static bool check_request_test_receipt(const char* file, char** options, uint32_t count, const char* overrides, CheckRequestReceipt* receipt)
{
    CheckSource source;
    const bool ready = check_source_prepare(&source, file, options, count, (CheckSourceRequest){ .emits = true });
    *receipt = ready ? check_request_receipt(source.pp, overrides) : (CheckRequestReceipt){ 0 };
    check_source_release(&source);
    return ready;
}

static void check_test_request_receipts(CheckTest* test, const char* directory, const char* file)
{
    char include[PATH_MAX];
    snprintf(include, sizeof(include), "-I%s", directory);
    static const char* const sets[][3] = {
        { "-nostdinc", 0 },
        { "-nostdinc", "-march=znver5", 0 },
        { "-nostdinc", "-DVALUE=2", 0 },
        { "-nostdinc", "-funsigned-char", 0 },
        { "-nostdinc", 0 },
    };
    enum { SETS = BUSTER_ARRAY_LENGTH(sets) };
    CheckRequestReceipt receipts[SETS + 2];
    bool ready = true;
    for (uint32_t s = 0; ready && s < SETS; s += 1)
    {
        char* options[3];
        uint32_t count = 0;
        while (count < 3 && sets[s][count] != 0)
        {
            options[count] = (char*)sets[s][count];
            count += 1;
        }
        ready = check_request_test_receipt(file, options, count, "path:vector", receipts + s);
    }
    char* included[] = { "-nostdinc", include };
    char* first[] = { "-nostdinc" };
    ready = ready && check_request_test_receipt(file, included, 2, "path:vector", receipts + SETS);
    ready = ready && check_request_test_receipt(file, first, 1, "path:scalar", receipts + SETS + 1);
    check_test_expect(test, ready, "request: the receipts' preparations");
    if (ready)
    {
        check_test_expect(test, receipts[0].digest == receipts[4].digest && strcmp(receipts[0].text, receipts[4].text) == 0,
                          "request: a file prepared twice with the same options has the same receipt");
        check_test_expect(test, strstr(receipts[0].text, " target=x86-64 ") != 0 && strstr(receipts[1].text, " target=znver5 ") != 0 &&
                                receipts[0].digest != receipts[1].digest,
                          "request: -march changes the receipt's target and digest");
        bool distinct = true;
        for (uint32_t s = 2; s < SETS + 2; s += 1)
        {
            const char* target = strstr(receipts[s].text, " target=");
            const char* language = strstr(receipts[s].text, " language=");
            const char* zero_target = strstr(receipts[0].text, " target=");
            const bool same_target = target != 0 && language != 0 && zero_target != 0 && strncmp(target, zero_target, (size_t)(language - target)) == 0;
            distinct &= s == 4 || (receipts[s].digest != receipts[0].digest && same_target);
        }
        check_test_expect(test, distinct, "request: -D, -funsigned-char, -I and the overrides each change the digest, not the target");
        check_test_expect(test, strstr(receipts[SETS + 1].text, " overrides=path:scalar") != 0 && strstr(receipts[0].text, " unwind=on debug=off ") != 0,
                          "request: the receipt names -c's output requirements and the client's overrides");
    }
}

typedef struct CheckRequestTestParity CheckRequestTestParity;
struct CheckRequestTestParity
{
    const char* label;
    uint32_t fixture;
    // The options before the file, ended by null.
    const char* options[5];
    bool scalar;
    // What -c and the preparation must both do: compile it (0), refuse its emission by name (1), report its target
    // features (2) or refuse the request (3).
    uint32_t outcome;
};

// `file` prepared with `options` as the source clients prepare it, then emitted, published and built as -c builds its
// object: whether that went as `outcome` says, with the object (malloc'd, the caller's to free) and its size for 0.
static bool check_request_test_object(const char* file, char** options, uint32_t count, bool scalar, uint32_t outcome, uint8_t** object, uint64_t* size)
{
    *object = NULL;
    *size = 0;
    CheckSource source;
    const bool ready = check_source_prepare(&source, file, options, count, (CheckSourceRequest){ .select = true, .emits = true, .scalar = scalar });
    Arena output = arena_reserve(UINT64_C(1) << 32);
    Arena bytes = arena_reserve(UINT64_C(1) << 32);
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    CheckDataResult emission = { .status = CHECK_DATA_RESOURCE };
    const CheckProgramSelection* selected = &source.selected;
    const Preprocessor* pp = source.pp;
    // -c's emission: the unit's unwind tables as clang has them, no rows without -g, no verification without --verify.
    const bool unwind = ready && predefines_unwind_tables(&pp->options.predefines, pp->options.freestanding);
    const bool emitted = ready && output.base != 0 && bytes.base != 0 &&
                         check_program_emit(&code, &selected->program, source.parser, pp->table, selected->data.capacity, BUSTER_SCALAR_PATH(scalar), &output, &bytes,
                                            &source.path.scratch, NULL, NULL, unwind, false);
    ObjectAtomSection* sections = 0;
    const bool published = emitted &&
                           check_data_publish(&selected->data, &source.path.types, selected->program.stream, selected->program.object_symbols,
                                              selected->program.object_count, &code.link, 0, &bytes, &source.path.scratch, &emission) &&
                           check_program_sections(&code, &selected->program, &bytes, &output, &sections);
    const Object built = published ? check_driver_object_build(&code, sections, NULL, file, &source.path.scratch) : (Object){ .error = OBJECT_ERROR_FULL };
    bool result = false;
    if (outcome == 0 && built.error == OBJECT_ERROR_NONE)
    {
        *object = malloc(built.size);
        result = *object != NULL;
        if (result)
        {
            memcpy(*object, built.bytes, built.size);
            *size = built.size;
        }
    }
    else if (outcome == 1)
    {
        result = ready && !emitted && code.status == CHECK_PROGRAM_UNSUPPORTED;
    }
    else if (outcome == 2)
    {
        result = source.stage == CHECK_SOURCE_TARGET_FEATURES && source.target_feature_errors != 0;
    }
    else if (outcome == 3)
    {
        result = source.stage == CHECK_SOURCE_OPTIONS && source.refusal.error != CHECK_REQUEST_ACCEPTED && check_request_test_zero(&source.options, sizeof(source.options));
    }
    arena_release(&bytes);
    arena_release(&output);
    check_source_release(&source);
    return result;
}

static void check_test_request_parity(CheckTest* test, const char* directory)
{
    static const char* const fixtures[] = {
        // Scalar code with static data, a string, a switch's jump table and a call.
        "static int table[4] = { 1, 2, 3, 5 };\nstatic const char* name = \"parity\";\n"
        "static int pick(int x)\n{\n    switch (x)\n    {\n        case 0: return 7;\n        case 1: return 11;\n        case 2: return 13;\n"
        "        case 3: return 17;\n        case 4: return 19;\n        default: return table[x & 3];\n    }\n}\n"
        "int main(void)\n{\n    return pick(name[0] & 7) + table[2] - 16;\n}\n",
        // 32-byte vectors: AVX2's registers.
        "typedef int V8 __attribute__((vector_size(32)));\nV8 add8(V8 a, V8 b)\n{\n    return a + b * a;\n}\n"
        "int main(void)\n{\n    V8 a = { 1, 2, 3, 4, 5, 6, 7, 8 };\n    return add8(a, a)[3] - 20;\n}\n",
        // 64-byte vectors: AVX-512's registers.
        "typedef int V16 __attribute__((vector_size(64)));\nV16 add16(V16 a, V16 b)\n{\n    return a + b;\n}\n"
        "int main(void)\n{\n    V16 a = { 1 };\n    return add16(a, a)[0] - 2;\n}\n",
        // A function whose target attribute takes AVX2's vectors in a unit of the baseline profile.
        "typedef int V8 __attribute__((vector_size(32)));\n__attribute__((target(\"avx2\"))) static int wide(int x)\n{\n    V8 a = { 0 };\n"
        "    a[0] = x;\n    V8 b = a + a;\n    return b[0];\n}\nint main(void)\n{\n    return wide(3) - 6;\n}\n",
        // Predefined and command-line macros that change the code.
        "#ifndef VALUE\n#define VALUE 3\n#endif\nint main(void)\n{\n#ifdef __CHAR_UNSIGNED__\n    const int sign = 1;\n#else\n    const int sign = -1;\n#endif\n"
        "#if __STDC_VERSION__ >= 201710L\n    const int version = 17;\n#else\n    const int version = 11;\n#endif\n    return VALUE * sign + version;\n}\n",
        // An always_inline callee whose target attribute its caller lacks: -c reports it at the caller.
        "__attribute__((target(\"avx2\"), always_inline)) static inline int twice(int x)\n{\n    return x * 2;\n}\n"
        "int main(void)\n{\n    return twice(1) - 2;\n}\n",
    };
    enum { FIXTURES = BUSTER_ARRAY_LENGTH(fixtures) };
    static const CheckRequestTestParity cases[] = {
        { .label = "scalar code for the baseline", .fixture = 0, .options = { "-nostdinc", 0 } },
        { .label = "scalar code for AVX2", .fixture = 0, .options = { "-nostdinc", "-march=x86-64-v3", 0 } },
        { .label = "scalar code for AVX-512", .fixture = 0, .options = { "-nostdinc", "-march=znver5", 0 } },
        { .label = "scalar code for the baseline by the scalar path", .fixture = 0, .options = { "-nostdinc", 0 }, .scalar = true },
        { .label = "32-byte vectors for AVX2", .fixture = 1, .options = { "-nostdinc", "-march=x86-64-v3", 0 } },
        { .label = "32-byte vectors refused for the baseline", .fixture = 1, .options = { "-nostdinc", 0 }, .outcome = 1 },
        { .label = "64-byte vectors for AVX-512", .fixture = 2, .options = { "-nostdinc", "-march=x86-64-v4", 0 } },
        { .label = "64-byte vectors refused for AVX2", .fixture = 2, .options = { "-nostdinc", "-march=x86-64-v3", 0 }, .outcome = 1 },
        { .label = "a function's target attribute in a baseline unit", .fixture = 3, .options = { "-nostdinc", 0 } },
        { .label = "a function's target attribute by the scalar path", .fixture = 3, .options = { "-nostdinc", 0 }, .scalar = true },
        { .label = "the default macros", .fixture = 4, .options = { "-nostdinc", 0 } },
        { .label = "-funsigned-char, -D and -std=c11", .fixture = 4, .options = { "-nostdinc", "-funsigned-char", "-DVALUE=5", "-std=c11", 0 } },
        { .label = "an always_inline callee's target the caller lacks", .fixture = 5, .options = { "-nostdinc", 0 }, .outcome = 2 },
        { .label = "an always_inline callee's target the unit has", .fixture = 5, .options = { "-nostdinc", "-march=x86-64-v3", 0 } },
        { .label = "a layout the checker doesn't implement", .fixture = 0, .options = { "-nostdinc", "-fshort-enums", 0 }, .outcome = 3 },
        { .label = "scalar code for the baseline, again last", .fixture = 0, .options = { "-nostdinc", 0 } },
    };
    char paths[FIXTURES][PATH_MAX];
    bool ready = true;
    for (uint32_t i = 0; ready && i < FIXTURES; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/parity_%u.c", directory, i);
        ready = parse_test_write(paths[i], fixtures[i], strlen(fixtures[i]));
    }
    check_test_expect(test, ready, "request: the parity fixtures are written");
    char object_path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(object_path, sizeof(object_path), "%s/parity.o", directory);
    snprintf(errors_path, sizeof(errors_path), "%s/parity.txt", directory);
    uint8_t* first = NULL;
    uint64_t first_size = 0;
    uint8_t* macros[2] = { NULL, NULL };
    uint64_t macro_sizes[2] = { 0, 0 };
    for (uint32_t c = 0; ready && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const CheckRequestTestParity* item = cases + c;
        char* options[8];
        uint32_t count = 0;
        while (item->options[count] != 0)
        {
            options[count] = (char*)item->options[count];
            count += 1;
        }
        char* arguments[16];
        uint32_t argument_count = 0;
        arguments[argument_count++] = "-c";
        if (item->scalar)
        {
            arguments[argument_count++] = "--scalar";
        }
        arguments[argument_count++] = "-o";
        arguments[argument_count++] = object_path;
        for (uint32_t i = 0; i < count; i += 1)
        {
            arguments[argument_count++] = options[i];
        }
        arguments[argument_count++] = paths[item->fixture];
        unlink(object_path);
        const int status = check_request_test_driver(arguments, argument_count, errors_path);
        uint64_t driver_size = 0;
        uint8_t* driver = item->outcome == 0 ? check_request_test_read(object_path, &driver_size) : NULL;
        uint8_t* object = NULL;
        uint64_t size = 0;
        const bool prepared = check_request_test_object(paths[item->fixture], options, count, item->scalar, item->outcome, &object, &size);
        const bool same = item->outcome == 0 ? status == 0 && driver != NULL && object != NULL && size == driver_size && memcmp(object, driver, size) == 0 :
                                               status == 1 && prepared;
        char label[256];
        snprintf(label, sizeof(label), "request: parity: %s: -c and the shared preparation %s", item->label,
                 item->outcome == 0 ? "make the same object" : item->outcome == 1 ? "refuse its emission" :
                 item->outcome == 2 ? "report its target features" : "refuse the request");
        check_test_expect(test, same, label);
        if (!same)
        {
            printf("request parity case \"%s\": driver status=%d object=%" PRIu64 " bytes, prepared=%d object=%" PRIu64 " bytes\n", item->label, status, driver_size,
                   prepared, size);
        }
        // The first case's object, made again after every other target, and the macros' two objects, which differ.
        if (c == 0)
        {
            first = object;
            first_size = size;
            object = NULL;
        }
        else if (c + 1 == BUSTER_ARRAY_LENGTH(cases))
        {
            check_test_expect(test, first != NULL && object != NULL && first_size == size && memcmp(first, object, size) == 0,
                              "request: parity: a preparation after other targets' makes the first one's object again");
        }
        else if (item->fixture == 4)
        {
            const uint32_t which = macros[0] == NULL ? 0 : 1;
            macros[which] = object;
            macro_sizes[which] = size;
            object = NULL;
        }
        free(object);
        free(driver);
    }
    check_test_expect(test, macros[0] != NULL && macros[1] != NULL && (macro_sizes[0] != macro_sizes[1] || memcmp(macros[0], macros[1], macro_sizes[0]) != 0),
                      "request: parity: the language and predefine switches change the object");
    free(macros[0]);
    free(macros[1]);
    free(first);
    for (uint32_t i = 0; i < FIXTURES; i += 1)
    {
        unlink(paths[i]);
    }
    unlink(object_path);
    unlink(errors_path);
}

static __attribute__((noinline)) void check_test_requests(CheckTest* test)
{
    char directory[] = "/tmp/buster-request-XXXXXX";
    const bool made = mkdtemp(directory) != 0;
    char file[PATH_MAX];
    snprintf(file, sizeof(file), "%s/request.c", directory);
    static const char source[] = "#ifndef VALUE\n#define VALUE 1\n#endif\nint main(void)\n{\n    return VALUE - 1;\n}\n";
    const bool ready = made && parse_test_write(file, source, strlen(source));
    check_test_expect(test, ready, "request: the fixtures' directory and file");
    if (ready)
    {
        check_test_request_refusals(test, directory, file);
        check_test_request_program_options(test);
        check_test_request_receipts(test, directory, file);
        check_test_request_parity(test, directory);
    }
    unlink(file);
    if (made)
    {
        rmdir(directory);
    }
}

#endif
