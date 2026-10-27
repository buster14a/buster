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

static CheckX86TestDiagnostic check_test_x86_diagnostic_read(const char* path)
{
    CheckX86TestDiagnostic result = { 0 };
    FILE* file = fopen(path, "r");
    char line[2048];
    while (file != NULL && fgets(line, sizeof(line), file) != NULL)
    {
        const char* message = strstr(line, "argument value ");
        if (message != NULL && strstr(message, "outside the valid range") != NULL)
        {
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

static void check_test_x86_diagnostic_storage(CheckTest* test)
{
    Arena scratch = arena_reserve(4096);
    check_test_expect(test, scratch.base != NULL, "x86 diagnostic scratch allocation");
    if (scratch.base != NULL)
    {
        CheckOp op = CHECK_OP_VALUE;
        uint32_t none = BUSTER_CHECK_NONE;
        CheckInput input = { .graph = { .count = 1, .ops = &op, .operand0 = &none, .operand1 = &none, .operand2 = &none } };
        CheckBuiltinX86Effect effect = { .range_event_count = 1 };
        CheckStream stream = { .x86 = &effect, .x86_count = 1 };
        CheckControl control = { .kind = CHECK_CONTROL_RETURN, .node = 0 };
        CheckProgramBody body = { .count = 1, .entry_count = 1 };
        CheckProgram program = { .input = &input, .stream = &stream, .controls = &control, .control_count = 1, .bodies = &body, .body_count = 1 };
        const uint64_t capacity = scratch.capacity;
        bool succeeded = false;
        for (uint32_t limit = 17; limit <= 64; limit += 1)
        {
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

static void check_test_x86_diagnostics(CheckTest* test, const char* clang)
{
    check_test_x86_diagnostic_storage(test);
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t diagnostics;
        bool rejected;
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
        { "initializer", "unsigned f(V a){unsigned x=BAD;return x;}", 1, true },
        { "dead initializer", "unsigned f(V a){if(0){unsigned x=BAD;return x;}return 0;}", 0, false },
        { "ordinary call argument", "void take(unsigned);void f(V a){take(BAD);}", 1, true },
        { "statement expression", "unsigned f(V a){return ({BAD;});}", 1, true },
        { "dead statement expression", "unsigned f(V a){return 0&&({BAD;});}", 0, false },
        { "warning policy", "#pragma clang diagnostic warning \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", 1, false },
        { "ignored policy", "#pragma clang diagnostic ignored \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}", 0, false },
        { "push pop policy", "#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wargument-outside-range\"\nunsigned f(V a){return BAD;}\n#pragma GCC diagnostic pop\nunsigned g(V a){return BAD;}", 1, true },
        { "ICE stays hard in dead code", "unsigned f(V a,int n){if(0)return __builtin_ia32_cmpb128_mask(a,a,n,65535);return 0;}", 0, true },
        { "ICE stays hard in sizeof", "unsigned f(V a,int n){return sizeof(__builtin_ia32_cmpb128_mask(a,a,n,65535));}", 0, true },
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
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", path, NULL };
        const int oracle_status = written ? object_test_run(arguments, output, errors) : -1;
        const CheckX86TestDiagnostic oracle = check_test_x86_diagnostic_read(errors);
        char description[512];
        snprintf(description, sizeof(description), "x86 %s: Clang status %d/ranges %u, expected %u/%u", fixtures[i].name,
                 oracle_status, oracle.count, fixtures[i].rejected, fixtures[i].diagnostics);
        check_test_expect(test, oracle_status == (fixtures[i].rejected ? 1 : 0) && oracle.count == fixtures[i].diagnostics, description);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const int status = written ? check_test_packed_run(NULL, path, output, errors, scalar != 0) : -1;
            const CheckX86TestDiagnostic actual = check_test_x86_diagnostic_read(errors);
            const bool same = status == oracle_status && actual.count == oracle.count &&
                              (actual.count == 0 || (actual.line == oracle.line && actual.column == oracle.column &&
                               actual.error == oracle.error && strcmp(actual.message, oracle.message) == 0));
            snprintf(description, sizeof(description), "x86 %s, scalar %u: status %d/ranges %u at %u:%u, Clang %d/%u at %u:%u: %.200s",
                     fixtures[i].name, scalar, status, actual.count, actual.line, actual.column, oracle_status, oracle.count,
                     oracle.line, oracle.column, actual.message);
            check_test_expect(test, same, description);
        }
    }
    if (ready)
    {
        unlink(path);
        unlink(output);
        unlink(errors);
        rmdir(directory);
    }
}
