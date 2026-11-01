// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source fixtures, each in its own translation unit: an earlier Clang error disables its deferred CFG
// analysis. Both production driver schedules must agree on exit status, range message, severity and location.

typedef struct CheckX86TestDiagnostic CheckX86TestDiagnostic;
struct CheckX86TestDiagnostic
{
    uint32_t count;
    uint32_t line;
    uint32_t column;
    bool error;
    char message[256];
};

static CheckX86TestDiagnostic check_test_x86_diagnostic_read(const char* path, bool range_only)
{
    CheckX86TestDiagnostic result = { 0 };
    FILE* file = fopen(path, "r");
    char line[2048];
    while (file != NULL && fgets(line, sizeof(line), file) != NULL)
    {
        const char* message = strstr(line, range_only ? "argument value " : ": error: ");
        if (message != NULL && (!range_only || strstr(message, "outside the valid range") != NULL))
        {
            message += range_only ? 0 : sizeof(": error: ") - 1;
            result.count += 1;
            result.error = strstr(line, "error: ") != NULL;
            sscanf(line, "%*[^:]:%u:%u:", &result.line, &result.column);
            const char* end = strstr(message, " [-W");
            end = end != NULL ? end : strchr(message, '\n');
            const uint32_t length = end != NULL ? (uint32_t)(end - message) : (uint32_t)strlen(message);
            snprintf(result.message, sizeof(result.message), "%.*s", (int)length, message);
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    else
    {
        result.count = UINT32_MAX;
    }
    return result;
}

static void check_test_x86_hard_diagnostic(CheckTest* test, const char* path, const char* name, const char* mode, uint32_t column)
{
    const CheckX86TestDiagnostic diagnostic = check_test_x86_diagnostic_read(path, false);
    const char* expected = strcmp(mode, "Clang") == 0 ?
                           "argument to '__builtin_ia32_cmpb128_mask' must be a constant integer" :
                           "incompatible operand or conversion types";
    char description[512];
    snprintf(description, sizeof(description), "x86 %s, %s: hard diagnostic count %u at %u:%u: %.200s",
             name, mode, diagnostic.count, diagnostic.line, diagnostic.column, diagnostic.message);
    // These fixtures each have one source line after the common three-line prefix.
    check_test_expect(test, diagnostic.count == 1 && diagnostic.error && diagnostic.line == 4 &&
                           diagnostic.column == column && strcmp(diagnostic.message, expected) == 0, description);
}

static void check_test_x86_diagnostic_storage(CheckTest* test)
{
    Arena scratch = arena_reserve(4096);
    check_test_expect(test, scratch.base != NULL, "x86 diagnostic scratch allocation");
    if (scratch.base != NULL)
    {
        CheckOp op = CHECK_OP_VALUE;
        uint32_t none = BUSTER_CHECK_NONE;
        CheckValue leaf = { 0 };
        uint32_t slot = 0;
        CheckInput input = { .graph = { .count = 1, .ops = &op, .leaves = &leaf, .leaf_slots = &slot, .leaf_count = 1, .operand0 = &none, .operand1 = &none,
                                        .operand2 = &none } };
        CheckBuiltinX86Effect effect = { .range_event_count = 1 };
        CheckStream stream = { .x86 = &effect, .x86_count = 1 };
        CheckControl control = { .kind = CHECK_CONTROL_RETURN, .node = 0 };
        CheckProgramBody body = { .count = 1, .entry_count = 1 };
        CheckProgram program = { .input = &input, .stream = &stream, .controls = &control, .control_count = 1, .bodies = &body, .body_count = 1 };
        const uint64_t capacity = scratch.capacity;
        bool succeeded = false;
        for (uint32_t limit = 17; limit <= 64; limit += 1)
        {
            assert(arena_commit(&scratch, scratch.capacity));
            memset(scratch.base, 0xa5, capacity);
            scratch.length = 17;
            scratch.capacity = limit;
            effect.range_events[0].disposition = CHECK_BUILTIN_X86_DEFERRED;
            const bool resolved = check_program_x86_diagnostics(&program, &stream, &scratch);
            bool prefix = scratch.length == 17;
            for (uint32_t i = 0; i < 17; i += 1)
            {
                prefix &= scratch.base[i] == 0xa5;
            }
            succeeded |= resolved;
            check_test_expect(test, prefix && effect.range_events[0].disposition ==
                                   (resolved ? CHECK_BUILTIN_X86_READY : CHECK_BUILTIN_X86_DEFERRED),
                              "x86 diagnostic exhaustion preserves deferred events and the scratch prefix");
        }
        check_test_expect(test, succeeded, "x86 diagnostic worklist fits its exact bounded allocation");
        scratch.capacity = capacity;
        program.body_count = 0;
        effect.range_events[0].disposition = CHECK_BUILTIN_X86_DEFERRED;
        check_test_expect(test, check_program_x86_diagnostics(&program, &stream, &scratch) &&
                               effect.range_events[0].disposition == CHECK_BUILTIN_X86_SUPPRESSED,
                          "an unevaluated graph node has no executable root");
        scratch.capacity = 0;
        scratch.length = 0;
        check_test_expect(test, check_program_x86_diagnostics(&program, &stream, &scratch) && scratch.length == 0,
                          "resolved x86 events need no scratch or second traversal");
        effect.range_event_count = 0;
        check_test_expect(test, check_program_x86_diagnostics(&program, &stream, &scratch) && scratch.length == 0,
                          "in-range x86 calls need no diagnostic worklist");
        scratch.capacity = capacity;
    }
    arena_release(&scratch);
}

static void check_test_x86_context_storage(CheckTest* test)
{
    Arena scratch = arena_reserve(4096);
    check_test_expect(test, scratch.base != NULL, "x86 source context scratch allocation");
    if (scratch.base != NULL)
    {
        uint8_t kinds[4] = { 0 };
        uint32_t selections[4] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t token = 0;
        Parser parser = { .tokens = { .count = 4, .kinds = kinds } };
        CheckX86Call call = { .reachability = CHECK_BUILTIN_X86_UNKNOWN };
        CheckInput input = { .graph = { .tokens = &token, .x86_calls = &call, .x86_count = 1 } };
        CheckInputReader reader = { .parser = &parser, .output = &input, .scratch = &scratch, .selected_tokens = selections };
        const uint64_t capacity = scratch.capacity;
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            parser.options.scalar = scalar != 0;
            bool succeeded = false;
            for (uint32_t limit = 17; limit <= 96; limit += 1)
            {
                assert(arena_commit(&scratch, scratch.capacity));
                memset(scratch.base, 0xa5, capacity);
                scratch.length = 17;
                scratch.capacity = limit;
                input.resource = false;
                call.unselected_context = true;
                call.runtime_diagnostic_suppressed = true;
                check_input_x86_contexts(&reader);
                bool prefix = scratch.length == 17;
                for (uint32_t i = 0; i < 17; i += 1)
                {
                    prefix &= scratch.base[i] == 0xa5;
                }
                succeeded |= !input.resource;
                check_test_expect(test, prefix && call.unselected_context == input.resource &&
                                       call.runtime_diagnostic_suppressed == input.resource && call.reachability == CHECK_BUILTIN_X86_UNKNOWN,
                                  "x86 source context exhaustion preserves metadata, executable reachability and scratch prefix");
            }
            check_test_expect(test, succeeded, "x86 source context columns fit their bounded allocation");
        }
        scratch.capacity = capacity;
    }
    arena_release(&scratch);
}

// Code generation's check of the x86 builtins' features (#148, c_check_target_feature.c) evaluates each builtin's
// expression against a table of predefines_features' names, without validating it each time, and takes any answer but
// available as missing, as clang's map has no other names: the table must be valid, and every x86 builtin of clang's
// metadata (the catalog the checker's intrinsic calls take their records from) must have an expression of those names
// alone, available with every feature and missing without them (but an empty one, which needs none).
static void check_test_target_feature_builtins(CheckTest* test)
{
    static CBuiltinIntrinsicFeature every[BUSTER_ARRAY_LENGTH(predefines_features)];
    static CBuiltinIntrinsicFeature none[BUSTER_ARRAY_LENGTH(predefines_features)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_features); i += 1)
    {
        const uint32_t length = (uint32_t)strlen(predefines_features[i].name);
        every[i] = (CBuiltinIntrinsicFeature){ .name = predefines_features[i].name, .name_length = length, .available = true };
        none[i] = (CBuiltinIntrinsicFeature){ .name = predefines_features[i].name, .name_length = length, .available = false };
    }
    check_test_expect(test, c_builtin_intrinsic_features_table_valid(every, BUSTER_ARRAY_LENGTH(every)) &&
                           c_builtin_intrinsic_features_table_valid(none, BUSTER_ARRAY_LENGTH(none)),
                      "the target-feature check's table of predefines_features' names is valid");
    uint32_t builtins = 0;
    uint32_t wrong = 0;
    const char* first = NULL;
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
        if (info->name_length > 15 && memcmp(info->name, "__builtin_ia32_", 15) == 0)
        {
            CBuiltinIntrinsicFeatureFrame frames[8];
            const CBuiltinIntrinsicFeatureStatus with = c_builtin_intrinsic_features_evaluate_valid(info->features, info->features_length, every,
                                                                                                   BUSTER_ARRAY_LENGTH(every), frames, BUSTER_ARRAY_LENGTH(frames));
            const CBuiltinIntrinsicFeatureStatus without = c_builtin_intrinsic_features_evaluate_valid(info->features, info->features_length, none,
                                                                                                      BUSTER_ARRAY_LENGTH(none), frames, BUSTER_ARRAY_LENGTH(frames));
            const bool right = with == CBUILTIN_INTRINSIC_FEATURE_AVAILABLE &&
                               without == (info->features_length == 0 ? CBUILTIN_INTRINSIC_FEATURE_AVAILABLE : CBUILTIN_INTRINSIC_FEATURE_MISSING);
            builtins += 1;
            wrong += !right;
            first = first == NULL && !right ? info->name : first;
        }
    }
    char description[256];
    snprintf(description, sizeof(description), "%u x86 builtins' features are expressions of predefines_features' names (%u not, the first %s)", builtins, wrong,
             first != NULL ? first : "none");
    check_test_expect(test, builtins != 0 && wrong == 0, description);
}

// The range warning's command-line mappings and their interaction with source pragmas (#1080): clang's severity and
// exit status must match both of the driver's paths, with and without an explicit source policy.
static void check_test_x86_command_line_mappings(CheckTest* test, const char* clang, const char* path, const char* output, const char* errors, bool ready)
{
    static const struct
    {
        const char* name;
        const char* source;
        const char* options[4];
        uint32_t option_count;
    } fixtures[] =
    {
        { "default", "unsigned f(V a){return BAD;}" },
        { "demoted", "unsigned f(V a){return BAD;}", { "-Wno-error=argument-outside-range" }, 1 },
        { "error then demoted", "unsigned f(V a){return BAD;}", { "-Werror", "-Wno-error=argument-outside-range" }, 2 },
        { "demoted then error", "unsigned f(V a){return BAD;}", { "-Wno-error=argument-outside-range", "-Werror" }, 2 },
        { "disabled", "unsigned f(V a){return BAD;}", { "-Wno-argument-outside-range" }, 1 },
        { "explicit error", "unsigned f(V a){return BAD;}", { "-Werror=argument-outside-range" }, 1 },
        { "disabled then enabled", "unsigned f(V a){return BAD;}", { "-Wno-argument-outside-range", "-Wargument-outside-range" }, 2 },
        { "everything disabled", "unsigned f(V a){return BAD;}", { "-Wno-everything" }, 1 },
        { "everything", "unsigned f(V a){return BAD;}", { "-Weverything" }, 1 },
        { "everything disabled then enabled", "unsigned f(V a){return BAD;}", { "-Wno-everything", "-Wargument-outside-range" }, 2 },
        { "error then everything disabled", "unsigned f(V a){return BAD;}", { "-Werror", "-Wno-everything" }, 2 },
        { "everything then demoted", "unsigned f(V a){return BAD;}", { "-Weverything", "-Wno-error=argument-outside-range" }, 2 },
        { "disabled then error", "unsigned f(V a){return BAD;}", { "-Wno-argument-outside-range", "-Werror" }, 2 },
        { "explicit error then no-error", "unsigned f(V a){return BAD;}", { "-Werror=argument-outside-range", "-Wno-error" }, 2 },
        { "demoted then error then error", "unsigned f(V a){return BAD;}", { "-Werror", "-Wno-error=argument-outside-range", "-Werror" }, 3 },
        { "unknown reachability demoted", "unsigned f(V a,int n){if(n)return BAD;return 0;}", { "-Wno-error=argument-outside-range" }, 1 },
        { "unknown reachability disabled", "unsigned f(V a,int n){if(n)return BAD;return 0;}", { "-Wno-argument-outside-range" }, 1 },
        { "disabled, warning pragma", "#pragma clang diagnostic warning \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", { "-Wno-argument-outside-range" }, 1 },
        { "demoted, error pragma", "#pragma clang diagnostic error \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", { "-Wno-error=argument-outside-range" }, 1 },
        { "error, warning pragma", "#pragma clang diagnostic warning \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", { "-Werror" }, 1 },
        { "error, ignored pragma", "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", { "-Werror" }, 1 },
        { "everything disabled, warning pragma", "#pragma clang diagnostic warning \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", { "-Wno-everything" }, 1 },
        { "demoted, GCC fatal pragma", "#pragma GCC diagnostic fatal \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", { "-Wno-error=argument-outside-range" }, 1 },
    };
    const char prefix[] = "typedef char V __attribute__((vector_size(16)));\n"
                          "typedef char W __attribute__((vector_size(64)));\n"
                          "#define BAD __builtin_ia32_cmpb128_mask(a,a,8,65535)\n";
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[2048];
        const int length = snprintf(source, sizeof(source), "%s%s\n", prefix, fixtures[i].source);
        const bool written = length > 0 && (size_t)length < sizeof(source) && parse_test_write(path, source, (size_t)length);
        char* arguments[16];
        uint32_t count = 0;
        arguments[count++] = (char*)clang;
        arguments[count++] = "-std=gnu17";
        arguments[count++] = "-fsyntax-only";
        arguments[count++] = "-fno-color-diagnostics";
        for (uint32_t o = 0; o < fixtures[i].option_count; o += 1)
        {
            arguments[count++] = (char*)fixtures[i].options[o];
        }
        arguments[count++] = (char*)path;
        arguments[count] = NULL;
        const int oracle_status = written ? object_test_run(arguments, output, errors) : -1;
        const CheckX86TestDiagnostic oracle = check_test_x86_diagnostic_read(errors, true);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const int status = written ? check_test_packed_run(NULL, path, output, errors, scalar != 0, false,
                                                               fixtures[i].options, fixtures[i].option_count) : -1;
            const CheckX86TestDiagnostic actual = check_test_x86_diagnostic_read(errors, true);
            const bool same = status == oracle_status && actual.count == oracle.count &&
                              (actual.count == 0 || (actual.line == oracle.line && actual.column == oracle.column &&
                               actual.error == oracle.error && strcmp(actual.message, oracle.message) == 0));
            char description[512];
            snprintf(description, sizeof(description), "x86 command-line %s, scalar %u: status %d/ranges %u at %u:%u (%s), Clang %d/%u at %u:%u (%s): %.200s",
                     fixtures[i].name, scalar, status, actual.count, actual.line, actual.column, actual.error ? "error" : "warning",
                     oracle_status, oracle.count, oracle.line, oracle.column, oracle.error ? "error" : "warning", actual.message);
            check_test_expect(test, same, description);
        }
    }
}

static void check_test_x86_diagnostics(CheckTest* test, const char* clang)
{
    check_test_target_feature_builtins(test);
    check_test_x86_diagnostic_storage(test);
    check_test_x86_context_storage(test);
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t diagnostics;
        bool rejected;
        bool quiet;
        uint32_t hard_columns[2]; // Clang and Buster locations for an expected hard-constraint error.
    } fixtures[] =
    {
        { "issue 487", "unsigned long long f(W a){return __builtin_ia32_ucmpb512_mask(a,a,8,~0ull);}", 1, true },
        { "negative immediate", "unsigned f(V a){return __builtin_ia32_cmpb128_mask(a,a,-1,65535);}", 1, true },
        { "in range", "unsigned f(V a){return __builtin_ia32_cmpb128_mask(a,a,7,65535);}", 0, false },
        { "unknown if", "unsigned f(V a,int n){if(n)return BAD;return 0;}", 1, true },
        { "live if", "unsigned f(V a){if(1)return BAD;return 0;}", 1, true },
        { "dead if", "unsigned f(V a){if(0)return BAD;return 0;}", 0, false },
        { "dead else", "unsigned f(V a){if(1)return 0;else return BAD;}", 0, false },
        { "after return", "unsigned f(V a){return 0;return BAD;}", 0, false },
        { "goto skips call", "unsigned f(V a){goto out;return BAD;out:return 0;}", 0, false },
        { "goto enters dead arm", "unsigned f(V a){goto in;if(0){in:return BAD;}return 0;}", 1, true },
        { "dead while", "unsigned f(V a){while(0)return BAD;return 0;}", 0, false },
        { "dead for", "unsigned f(V a){for(;0;)return BAD;return 0;}", 0, false },
        { "live do", "unsigned f(V a){do{return BAD;}while(0);}", 1, true },
        { "after break", "unsigned f(V a){while(1){break;return BAD;}return 0;}", 0, false },
        { "after continue", "unsigned f(V a,int n){while(n--){continue;return BAD;}return 0;}", 0, false },
        { "dead switch case", "unsigned f(V a){switch(0){case 1:return BAD;default:return 0;}}", 0, false },
        { "live switch case", "unsigned f(V a){switch(0){case 0:return BAD;default:return 0;}}", 1, true },
        { "live switch default", "unsigned f(V a){switch(0){case 1:return 0;default:return BAD;}}", 1, true },
        { "switch fallthrough", "unsigned f(V a){switch(0){case 0:;case 1:return BAD;default:return 0;}}", 1, true },
        { "unknown switch", "unsigned f(V a,int n){switch(n){case 1:return BAD;default:return 0;}}", 1, true },
        { "short circuit and", "unsigned f(V a){return 0&&BAD;}", 0, false },
        { "short circuit or", "unsigned f(V a){return 1||BAD;}", 0, false },
        { "live and", "unsigned f(V a){return 1&&BAD;}", 1, true },
        { "unknown and", "unsigned f(V a,int n){return n&&BAD;}", 1, true },
        { "dead conditional arm", "unsigned f(V a){return 1?0:BAD;}", 0, false },
        { "live conditional arm", "unsigned f(V a){return 0?0:BAD;}", 1, true },
        { "sizeof", "unsigned f(V a){return sizeof(BAD);}", 0, false },
        { "alignof", "unsigned f(V a){return __alignof__(BAD);}", 0, false },
        { "selected choose", "unsigned f(V a){return __builtin_choose_expr(1,BAD,0);}", 1, true },
        { "selected false choose", "unsigned f(V a){return __builtin_choose_expr(0,0,BAD);}", 1, true },
        { "unselected true choose", "unsigned f(V a){return __builtin_choose_expr(0,BAD,0);}", 1, true },
        { "unselected false choose", "unsigned f(V a){return __builtin_choose_expr(1,0,BAD);}", 1, true },
        { "in-range unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,__builtin_ia32_cmpb128_mask(a,a,7,65535),0);}", 0, false },
        { "dead selected choose", "unsigned f(V a){if(0)return __builtin_choose_expr(1,BAD,0);return 0;}", 0, false },
        { "dead unselected choose", "unsigned f(V a){if(0)return __builtin_choose_expr(0,BAD,0);return 0;}", 1, true },
        { "unselected choose after return", "unsigned f(V a){return 0;return __builtin_choose_expr(0,BAD,0);}", 1, true },
        { "unselected choose after goto", "unsigned f(V a){goto out;return __builtin_choose_expr(1,0,BAD);out:return 0;}", 1, true },
        { "short-circuited selected choose", "unsigned f(V a){return 0&&__builtin_choose_expr(1,BAD,0);}", 0, false },
        { "short-circuited unselected choose", "unsigned f(V a){return 0&&__builtin_choose_expr(0,BAD,0);}", 1, true },
        { "short circuit in unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,0&&BAD,0);}", 1, true },
        { "conditional in unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,1?0:BAD,0);}", 1, true },
        { "statement in unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,({if(0)BAD;0;}),0);}", 1, true },
        { "compound literal in unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,(unsigned){BAD},0);}", 1, true },
        { "nested unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,__builtin_choose_expr(0,BAD,0),0);}", 1, true },
        { "selected choose inside unselected choose", "unsigned f(V a){return __builtin_choose_expr(1,0,__builtin_choose_expr(1,BAD,0));}", 1, true },
        { "unselected choose inside selected choose", "unsigned f(V a){return __builtin_choose_expr(1,__builtin_choose_expr(1,0,BAD),0);}", 1, true },
        { "sizeof selected choose", "unsigned f(V a){return sizeof(__builtin_choose_expr(1,BAD,0));}", 0, false },
        { "sizeof unselected choose", "unsigned f(V a){return sizeof(__builtin_choose_expr(0,BAD,0));}", 0, false },
        { "review sizeof VLA bound", "unsigned f(V a,int n){(void)__builtin_choose_expr(1,0,0);return sizeof(int[n+BAD]);}", 0, false },
        { "unselected choose in sizeof VLA bound", "unsigned f(V a,int n){return sizeof(int[n+__builtin_choose_expr(0,BAD,1)]);}", 0, false },
        { "selected choose in sizeof VLA bound", "unsigned f(V a,int n){return sizeof(int[n+__builtin_choose_expr(1,BAD,1)]);}", 0, false },
        { "sizeof VLA in unselected choose", "unsigned f(V a,int n){return __builtin_choose_expr(0,sizeof(int[n+BAD]),0);}", 0, false },
        { "nested sizeof VLA bound", "unsigned f(V a,int n){(void)__builtin_choose_expr(1,0,0);return sizeof(sizeof(int[n+BAD]));}", 0, false },
        { "in-range sizeof VLA bound", "unsigned f(V a,int n){(void)__builtin_choose_expr(1,0,0);return sizeof(int[n+__builtin_ia32_cmpb128_mask(a,a,7,65535)]);}", 0, false },
        { "alignof unselected choose", "unsigned f(V a){return __alignof__(__builtin_choose_expr(0,BAD,0));}", 0, false },
        { "sizeof inside unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,sizeof(BAD),0);}", 0, false },
        { "choose inside unselected sizeof", "unsigned f(V a){return __builtin_choose_expr(0,sizeof(__builtin_choose_expr(0,BAD,0)),0);}", 0, false },
        { "dead sizeof unselected choose", "unsigned f(V a){if(0)return sizeof(__builtin_choose_expr(0,BAD,0));return 0;}", 0, false },
        { "typeof unselected choose", "unsigned f(V a){typeof(__builtin_choose_expr(0,BAD,0)) x=0;return x;}", 0, false },
        { "unselected generic association", "unsigned f(V a){return _Generic(0,int:0,default:BAD);}", 1, true },
        { "choose in generic control", "unsigned f(V a){return _Generic(__builtin_choose_expr(0,BAD,0),int:0,default:1);}", 0, false },
        { "sizeof generic association", "unsigned f(V a){return sizeof(_Generic(0,int:0,default:BAD));}", 0, false },
        { "ignored unselected choose", "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\nunsigned f(V a){return __builtin_choose_expr(0,BAD,0);}", 0, false },
        { "warning unselected choose", "#pragma clang diagnostic warning \"-Wargument-outside-range\"\nunsigned f(V a){if(0)return __builtin_choose_expr(1,0,BAD);return 0;}", 1, false },
        { "push pop unselected choose", "#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wargument-outside-range\"\nunsigned f(V a){return __builtin_choose_expr(0,BAD,0);}\n#pragma GCC diagnostic pop\nunsigned g(V a){return __builtin_choose_expr(0,BAD,0);}", 1, true },
        { "-w suppresses unselected choose", "unsigned f(V a){return __builtin_choose_expr(0,BAD,0);}", 0, false, true },
        { "ICE stays hard in unselected choose", "unsigned f(V a,int n){return __builtin_choose_expr(0,__builtin_ia32_cmpb128_mask(a,a,n,65535),0);}", 0, true },
        { "initializer", "unsigned f(V a){unsigned x=BAD;return x;}", 1, true },
        { "dead initializer", "unsigned f(V a){if(0){unsigned x=BAD;return x;}return 0;}", 0, false },
        { "scalar compound literal", "unsigned f(V a){return (unsigned){BAD};}", 1, true },
        { "array compound literal", "unsigned f(V a){return ((unsigned[]){BAD,0})[0];}", 1, true },
        { "record compound literal", "struct S{unsigned x;};unsigned f(V a){return ((struct S){.x=BAD}).x;}", 1, true },
        { "nested compound literal", "unsigned f(V a){return (unsigned){(unsigned){BAD}};}", 1, true },
        { "in-range compound literal", "unsigned f(V a){return (unsigned){__builtin_ia32_cmpb128_mask(a,a,7,65535)};}", 0, false },
        { "dead compound literal", "unsigned f(V a){if(0)return (unsigned){BAD};return 0;}", 0, false },
        { "sizeof compound literal", "unsigned f(V a){return sizeof((unsigned){BAD});}", 0, false },
        { "short-circuited compound literal", "unsigned f(V a){return 0&&((unsigned){BAD});}", 0, false },
        { "ordinary call argument", "void take(unsigned);void f(V a){take(BAD);}", 1, true },
        { "statement expression", "unsigned f(V a){return ({BAD;});}", 1, true },
        { "dead statement expression", "unsigned f(V a){return 0&&({BAD;});}", 0, false },
        { "warning policy", "#pragma clang diagnostic warning \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", 1, false },
        { "ignored policy", "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", 0, false },
        { "push pop policy", "#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}\n#pragma GCC diagnostic pop\nunsigned g(V a){return BAD;}", 1, true },
        { "ICE stays hard in dead code", "unsigned f(V a,int n){if(0)return __builtin_ia32_cmpb128_mask(a,a,n,65535);return 0;}", 0, true, false, { 35, 74 } },
        { "ICE stays hard in sizeof", "unsigned f(V a,int n){return sizeof(__builtin_ia32_cmpb128_mask(a,a,n,65535));}", 0, true, false, { 37, 76 } },
        { "-w suppresses range", "unsigned f(V a){return BAD;}", 0, false, true },
        { "-w suppresses compound literal range", "unsigned f(V a){return (unsigned){BAD};}", 0, false, true },
        { "ICE stays hard with -w", "unsigned f(V a,int n){return __builtin_ia32_cmpb128_mask(a,a,n,65535);}", 0, true, true, { 30, 69 } },
    };
    char directory[] = "/tmp/buster-x86-diagnostic-XXXXXX";
    const bool ready = mkdtemp(directory) != NULL;
    check_test_expect(test, ready, "x86 range diagnostic directory");
    char path[256];
    char output[256];
    char errors[256];
    snprintf(path, sizeof(path), "%s/source.c", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    const char prefix[] = "typedef char V __attribute__((vector_size(16)));\n"
                          "typedef char W __attribute__((vector_size(64)));\n"
                          "#define BAD __builtin_ia32_cmpb128_mask(a,a,8,65535)\n";
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[2048];
        const int length = snprintf(source, sizeof(source), "%s%s\n", prefix, fixtures[i].source);
        const bool written = length > 0 && (size_t)length < sizeof(source) && parse_test_write(path, source, (size_t)length);
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", fixtures[i].quiet ? "-w" : "-fsyntax-only", path, NULL };
        const int oracle_status = written ? object_test_run(arguments, output, errors) : -1;
        const CheckX86TestDiagnostic oracle = check_test_x86_diagnostic_read(errors, true);
        char description[512];
        snprintf(description, sizeof(description), "x86 %s: Clang status %d/ranges %u, expected %u/%u", fixtures[i].name,
                 oracle_status, oracle.count, fixtures[i].rejected, fixtures[i].diagnostics);
        check_test_expect(test, oracle_status == (fixtures[i].rejected ? 1 : 0) && oracle.count == fixtures[i].diagnostics, description);
        if (fixtures[i].hard_columns[0] != 0)
        {
            check_test_x86_hard_diagnostic(test, errors, fixtures[i].name, "Clang", fixtures[i].hard_columns[0]);
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const int status = written ? check_test_packed_run(NULL, path, output, errors, scalar != 0, fixtures[i].quiet, NULL, 0) : -1;
            const CheckX86TestDiagnostic actual = check_test_x86_diagnostic_read(errors, true);
            const bool same = status == oracle_status && actual.count == oracle.count &&
                              (actual.count == 0 || (actual.line == oracle.line && actual.column == oracle.column &&
                               actual.error == oracle.error && strcmp(actual.message, oracle.message) == 0));
            snprintf(description, sizeof(description), "x86 %s, scalar %u: status %d/ranges %u at %u:%u, Clang %d/%u at %u:%u: %.200s",
                     fixtures[i].name, scalar, status, actual.count, actual.line, actual.column, oracle_status, oracle.count,
                     oracle.line, oracle.column, actual.message);
            check_test_expect(test, same, description);
            if (fixtures[i].hard_columns[1] != 0)
            {
                check_test_x86_hard_diagnostic(test, errors, fixtures[i].name, scalar ? "scalar" : "vector", fixtures[i].hard_columns[1]);
            }
        }
    }
    check_test_x86_command_line_mappings(test, clang, path, output, errors, ready);
    if (ready)
    {
        unlink(path);
        unlink(output);
        unlink(errors);
        rmdir(directory);
    }
}
