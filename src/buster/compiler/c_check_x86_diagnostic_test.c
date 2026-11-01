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
    char description[512];
    snprintf(description, sizeof(description), "x86 %s, %s: hard diagnostic count %u at %u:%u: %.200s",
             name, mode, diagnostic.count, diagnostic.line, diagnostic.column, diagnostic.message);
    // These fixtures each have one source line after the common three-line prefix. Clang and both paths report the
    // nonconstant immediate at the call's begin, naming the builtin (#1066).
    check_test_expect(test, diagnostic.count == 1 && diagnostic.error && diagnostic.line == 4 && diagnostic.column == column &&
                           strcmp(diagnostic.message, "argument to '__builtin_ia32_cmpb128_mask' must be a constant integer") == 0, description);
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

// The diagnostics of a run in `path`, each line that starts with the source's name and a colon (`FILE:LINE:COLUMN: ...`:
// errors, warnings and notes, not the counts' line), the name left out, joined by newlines into `text`, `capacity` bytes.
// Returns their count, or UINT32_MAX where the file can't be read or the lines don't fit. `capacity` is at least 1.
static uint32_t check_test_x86_diagnostic_lines(const char* path, const char* source, char* text, uint32_t capacity)
{
    uint32_t result = 0;
    uint32_t used = 0;
    const size_t name = strlen(source);
    FILE* file = fopen(path, "r");
    char line[4096];
    while (file != NULL && result != UINT32_MAX && fgets(line, sizeof(line), file) != NULL)
    {
        if (strncmp(line, source, name) == 0 && line[name] == ':')
        {
            const size_t length = strlen(line + name + 1);
            if (used + length + 2 < capacity)
            {
                memcpy(text + used, line + name + 1, length);
                used += (uint32_t)length;
                if (length == 0 || text[used - 1] != '\n')
                {
                    text[used++] = '\n';
                }
                result += 1;
            }
            else
            {
                result = UINT32_MAX;
            }
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    else
    {
        result = UINT32_MAX;
    }
    text[used] = 0;
    return result;
}

// The driver on `source` in a child, as a command line runs it: `-fsyntax-only`, or with `compile` `-c` into `object`, by the
// vector path or with `scalar` by the scalar one, its output and errors into `output` and `errors`. Returns its exit status.
static int check_test_x86_hard_run(const char* source, const char* object, const char* output, const char* errors, bool scalar, bool compile)
{
    int result = -1;
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        const int out = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const int err = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out < 0 || err < 0 || dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0)
        {
            _exit(126);
        }
        close(out);
        close(err);
        char* arguments[10];
        int count = 0;
        if (scalar)
        {
            arguments[count++] = "--scalar";
        }
        if (compile)
        {
            arguments[count++] = "-c";
            arguments[count++] = "-o";
            arguments[count++] = (char*)object;
        }
        else
        {
            arguments[count++] = "-fsyntax-only";
        }
        arguments[count++] = "-ferror-limit=0";
        arguments[count++] = "-march=x86-64-v4";
        arguments[count++] = "-std=gnu17";
        arguments[count++] = (char*)source;
        const int code = run_check_command(count, arguments, true);
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

// `source` checked by clang and by the driver four ways, the vector and the scalar path, `-fsyntax-only` and `-c`: each of
// the driver's runs must fail as clang's does, with clang's diagnostics, line for line (message, place and order, and the
// notes of the macros they come from). Clang's `expected` diagnostics, when given, are its own check.
static void check_test_x86_hard_compare(CheckTest* test, const char* clang, const char* name, const char* source, const char* object,
                                        const char* output, const char* errors, const char* expected, uint32_t count, char* text, char* other,
                                        uint32_t capacity)
{
    char* arguments[] = { (char*)clang, "-fsyntax-only", "-fno-caret-diagnostics", "-fno-color-diagnostics", "-ferror-limit=0", "-march=x86-64-v4",
                          "-std=gnu17", (char*)source, NULL };
    const int oracle_status = object_test_run(arguments, output, errors);
    const uint32_t lines = check_test_x86_diagnostic_lines(errors, source, text, capacity);
    char description[1024];
    snprintf(description, sizeof(description), "x86 hard errors, %s: Clang status %d and %u diagnostic lines, expected 1 and %u%s%.600s", name, oracle_status,
             lines, count, expected != NULL ? ":\n" : "", expected != NULL ? text : "");
    check_test_expect(test, oracle_status == 1 && lines == count && (expected == NULL || strcmp(text, expected) == 0), description);
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        const bool scalar = (mode & 1) != 0;
        const bool compile = (mode & 2) != 0;
        unlink(object);
        const int status = check_test_x86_hard_run(source, object, output, errors, scalar, compile);
        const uint32_t ours = check_test_x86_diagnostic_lines(errors, source, other, capacity);
        uint32_t offset = 0;
        while (text[offset] != 0 && text[offset] == other[offset])
        {
            offset += 1;
        }
        while (offset != 0 && text[offset - 1] != '\n')
        {
            offset -= 1;
        }
        snprintf(description, sizeof(description), "x86 hard errors, %s, %s %s: status %d and %u diagnostic lines, Clang's %d and %u; first difference:\n"
                 "clang: %.300s\nours:  %.300s", name, scalar ? "scalar" : "vector", compile ? "-c" : "-fsyntax-only", status, ours, oracle_status, lines,
                 text + offset, other + offset);
        check_test_expect(test, status == oracle_status && ours == lines && strcmp(text, other) == 0 && access(object, F_OK) != 0, description);
    }
}

// An x86 builtin's call whose hard immediate constraint fails, against clang (#1066): a nonconstant immediate's error
// names the builtin, and every one of them is at the call's begin, the builtin's name, through macros too, in clang's
// order among the unit's other errors, whether the driver checks the unit or compiles it.
static void check_test_x86_hard_errors(CheckTest* test, const char* clang)
{
    static const struct
    {
        const char* name;
        const char* source;
        // Clang's diagnostics (each "LINE:COLUMN: severity: message"), the lines' numbers past the prefix's.
        const char* expected;
    } fixtures[] =
    {
        { "issue 1066", "unsigned f(unsigned b,unsigned i){return __builtin_ia32_kshiftlisi(b,i&31?1:0);}\n",
          "8:42: error: argument to '__builtin_ia32_kshiftlisi' must be a constant integer\n" },
        { "every k-shift",
          "unsigned char a(unsigned char b,unsigned i){return __builtin_ia32_kshiftliqi(b,i);}\n"
          "unsigned short c(unsigned short b,unsigned i){return __builtin_ia32_kshiftlihi(b,i);}\n"
          "unsigned d(unsigned b,unsigned i){return __builtin_ia32_kshiftlisi(b,i);}\n"
          "unsigned long long e(unsigned long long b,unsigned i){return __builtin_ia32_kshiftlidi(b,i);}\n"
          "unsigned char g(unsigned char b,unsigned i){return __builtin_ia32_kshiftriqi(b,i);}\n"
          "unsigned short h(unsigned short b,unsigned i){return __builtin_ia32_kshiftrihi(b,i);}\n"
          "unsigned j(unsigned b,unsigned i){return __builtin_ia32_kshiftrisi(b,i);}\n"
          "unsigned long long k(unsigned long long b,unsigned i){return __builtin_ia32_kshiftridi(b,i);}\n",
          "8:52: error: argument to '__builtin_ia32_kshiftliqi' must be a constant integer\n"
          "9:54: error: argument to '__builtin_ia32_kshiftlihi' must be a constant integer\n"
          "10:42: error: argument to '__builtin_ia32_kshiftlisi' must be a constant integer\n"
          "11:62: error: argument to '__builtin_ia32_kshiftlidi' must be a constant integer\n"
          "12:52: error: argument to '__builtin_ia32_kshiftriqi' must be a constant integer\n"
          "13:54: error: argument to '__builtin_ia32_kshiftrihi' must be a constant integer\n"
          "14:42: error: argument to '__builtin_ia32_kshiftrisi' must be a constant integer\n"
          "15:62: error: argument to '__builtin_ia32_kshiftridi' must be a constant integer\n" },
        { "call over two lines", "unsigned f(unsigned b,unsigned i){return __builtin_ia32_kshiftrisi(b,\n    i&31?1:0);}\n",
          "8:42: error: argument to '__builtin_ia32_kshiftrisi' must be a constant integer\n" },
        { "two calls on one line", "unsigned f(unsigned b,unsigned i){return __builtin_ia32_kshiftlisi(b,i)|__builtin_ia32_kshiftrisi(b,i+1);}\n",
          "8:42: error: argument to '__builtin_ia32_kshiftlisi' must be a constant integer\n"
          "8:73: error: argument to '__builtin_ia32_kshiftrisi' must be a constant integer\n" },
        { "nested calls", "unsigned f(unsigned b,unsigned i){return __builtin_ia32_kshiftlisi(__builtin_ia32_kshiftrisi(b,i),1);}\n",
          "8:68: error: argument to '__builtin_ia32_kshiftrisi' must be a constant integer\n" },
        { "macro", "#define SHIFT(x,n) __builtin_ia32_kshiftlidi(x,n)\nunsigned long long f(unsigned long long b,unsigned i){return SHIFT(b,i);}\n",
          "9:62: error: argument to '__builtin_ia32_kshiftlidi' must be a constant integer\n"
          "8:20: note: expanded from macro 'SHIFT'\n" },
        { "macro argument", "#define KEEP(e) (e)\nunsigned f(unsigned b,unsigned i){return KEEP(__builtin_ia32_kshiftlisi(b,i));}\n",
          "9:47: error: argument to '__builtin_ia32_kshiftlisi' must be a constant integer\n" },
        { "nested macros", "#define SHIFT(x,n) __builtin_ia32_kshiftlihi(x,n)\n#define TWICE(x,n) SHIFT(SHIFT(x,1),n)\n"
          "unsigned short f(unsigned short b,unsigned i){return TWICE(b,i);}\n",
          "10:54: error: argument to '__builtin_ia32_kshiftlihi' must be a constant integer\n"
          "9:20: note: expanded from macro 'TWICE'\n"
          "8:20: note: expanded from macro 'SHIFT'\n" },
        { "constants and not", "enum{N=3};const unsigned c=3;\n"
          "unsigned g(unsigned b){return __builtin_ia32_kshiftlisi(b,(int)1.5)+__builtin_ia32_kshiftrisi(b,sizeof(int))+__builtin_ia32_kshiftlisi(b,N);}\n"
          "unsigned f(unsigned b,double d){return __builtin_ia32_kshiftlisi(b,d)+__builtin_ia32_kshiftrisi(b,1.0)+__builtin_ia32_kshiftlisi(b,c);}\n",
          "10:40: error: argument to '__builtin_ia32_kshiftlisi' must be a constant integer\n"
          "10:71: error: argument to '__builtin_ia32_kshiftrisi' must be a constant integer\n"
          "10:104: error: argument to '__builtin_ia32_kshiftlisi' must be a constant integer\n" },
        { "dead code and sizeof",
          "unsigned f(unsigned b,unsigned i){if(0)return __builtin_ia32_kshiftlisi(b,i);return sizeof(__builtin_ia32_kshiftrisi(b,i));}\n",
          "8:47: error: argument to '__builtin_ia32_kshiftlisi' must be a constant integer\n"
          "8:92: error: argument to '__builtin_ia32_kshiftrisi' must be a constant integer\n" },
        { "other immediates",
          "I4 a(I4 x,int n){return __builtin_ia32_pshufd(x,n);}\n"
          "F4 b(F4 x,F4 y,int n){return __builtin_ia32_shufps(x,y,n);}\n"
          "int c(I4 x,int n){return __builtin_ia32_vec_ext_v4si(x,n);}\n"
          "V d(V x,V y,int n){return __builtin_ia32_palignr128(x,y,n);}\n"
          "unsigned short e(V x,int n){return __builtin_ia32_cmpb128_mask(x,x,n,65535);}\n"
          "I8 g(D8 x,I8 old,int n){return __builtin_ia32_cvtpd2dq512_mask(x,old,255,n);}\n"
          "I16 h(I16 i,const void* p,int n){return __builtin_ia32_gathersiv16si(i,p,i,65535,n);}\n",
          "8:25: error: argument to '__builtin_ia32_pshufd' must be a constant integer\n"
          "9:30: error: argument to '__builtin_ia32_shufps' must be a constant integer\n"
          "10:26: error: argument to '__builtin_ia32_vec_ext_v4si' must be a constant integer\n"
          "11:27: error: argument to '__builtin_ia32_palignr128' must be a constant integer\n"
          "12:36: error: argument to '__builtin_ia32_cmpb128_mask' must be a constant integer\n"
          "13:32: error: argument to '__builtin_ia32_cvtpd2dq512_mask' must be a constant integer\n"
          "14:41: error: argument to '__builtin_ia32_gathersiv16si' must be a constant integer\n" },
        { "rounding and scale",
          "I8 a(D8 x,I8 old){return __builtin_ia32_cvtpd2dq512_mask(x,old,255,12);}\n"
          "int b(D2 x){return __builtin_ia32_vcvttsd2si32(x,9);}\n"
          "I16 c(I16 i,const void* p){return __builtin_ia32_gathersiv16si(i,p,i,65535,3);}\n"
          "F4 d(F4 x,F4 y){return __builtin_ia32_shufps(x,y,(int)2.5)+__builtin_ia32_shufps(x,y,9);}\n",
          "8:26: error: invalid rounding argument\n"
          "9:20: error: invalid rounding argument\n"
          "10:35: error: scale argument must be 1, 2, 4, or 8\n" },
    };
    char directory[] = "/tmp/buster-x86-hard-XXXXXX";
    const bool ready = mkdtemp(directory) != NULL;
    check_test_expect(test, ready, "x86 hard error directory");
    const uint32_t capacity = 1u << 16;
    char* text = malloc(capacity);
    char* other = malloc(capacity);
    check_test_expect(test, text != NULL && other != NULL, "x86 hard error buffers");
    char path[256];
    char object[256];
    char output[256];
    char errors[256];
    snprintf(path, sizeof(path), "%s/source.c", directory);
    snprintf(object, sizeof(object), "%s/source.o", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    const char prefix[] = "typedef char V __attribute__((vector_size(16)));\n"
                          "typedef int I4 __attribute__((vector_size(16)));\n"
                          "typedef float F4 __attribute__((vector_size(16)));\n"
                          "typedef double D2 __attribute__((vector_size(16)));\n"
                          "typedef double D8 __attribute__((vector_size(64)));\n"
                          "typedef int I8 __attribute__((vector_size(32)));\n"
                          "typedef int I16 __attribute__((vector_size(64)));\n";
    for (uint32_t i = 0; ready && text != NULL && other != NULL && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[4096];
        const int length = snprintf(source, sizeof(source), "%s%s", prefix, fixtures[i].source);
        const bool written = length > 0 && (size_t)length < sizeof(source) && parse_test_write(path, source, (size_t)length);
        check_test_expect(test, written, "x86 hard error fixture written");
        uint32_t lines = 0;
        for (const char* at = fixtures[i].expected; *at != 0; at += 1)
        {
            lines += *at == '\n';
        }
        if (written)
        {
            check_test_x86_hard_compare(test, clang, fixtures[i].name, path, object, output, errors, fixtures[i].expected, lines, text, other, capacity);
        }
    }
    free(text);
    free(other);
    if (ready)
    {
        unlink(path);
        unlink(object);
        unlink(output);
        unlink(errors);
        rmdir(directory);
    }
}

// Every x86 builtin the checker takes by its prototype and SemaX86's rules (builtin_intrinsic_own_main_prototype_in_scope)
// called with each argument clang requires to be an integer constant expression (the prototype's `I` ones and the rules'
// arguments, check_graph_x86_ice_mask) as a parameter, a function each, its other such arguments values the rules take:
// clang must refuse every call at its builtin's name, naming it, and the driver must give clang's diagnostics (#1066).
// Before it, every immediate and rounding argument of the lowering's rows (c_builtin_x86_rows.inc) must be such an
// argument, so that the checker refuses a nonconstant one as clang does, never leaving it to the lowering's refusal.
static void check_test_x86_hard_every(CheckTest* test, TypeTable* table, const char* clang)
{
    uint32_t rows = 0;
    uint32_t loose = 0;
    const char* first_loose = NULL;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); r += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + r;
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(rule->name, rule->name_length);
        BuiltinIntrinsicTypeResult types = { 0 };
        if (info != NULL)
        {
            const BuiltinIntrinsicTypeInput input = {
                .encoding = info->encoding,
                .encoding_length = info->encoding_length,
                .attributes = info->attributes,
                .attributes_length = info->attributes_length,
                .custom_typechecking = info->custom_typechecking,
            };
            builtin_intrinsic_types_decode(table, &input, &types);
        }
        const uint64_t required = types.ice_parameters | check_graph_x86_ice_mask(info, false);
        const bool immediate = rule->immediate != BUSTER_CHECK_X86_NO_IMMEDIATE;
        const bool rounding = rule->rounding != CHECK_X86_ROUND_NONE;
        if (immediate || rounding)
        {
            const uint32_t last = types.parameter_count != 0 ? types.parameter_count - 1 : 0;
            const bool right = info != NULL && types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS &&
                               (!immediate || (rule->immediate < types.parameter_count && (required & (UINT64_C(1) << rule->immediate)) != 0)) &&
                               (!rounding || (types.parameter_count != 0 && (required & (UINT64_C(1) << last)) != 0));
            rows += 1;
            loose += !right;
            first_loose = first_loose == NULL && !right ? rule->name : first_loose;
        }
    }
    char description[1024];
    snprintf(description, sizeof(description), "%u x86 lowering rows' immediates and rounding controls are integer constant arguments (%u not, the first %s)", rows,
             loose, first_loose != NULL ? first_loose : "none");
    check_test_expect(test, rows != 0 && loose == 0, description);
    const uint32_t capacity = 1u << 22;
    char* source = malloc(capacity);
    char* expected = malloc(capacity);
    char* text = malloc(capacity);
    char* other = malloc(capacity);
    Arena names = arena_reserve(UINT64_C(1) << 20);
    char directory[] = "/tmp/buster-x86-every-XXXXXX";
    bool ready = source != NULL && expected != NULL && text != NULL && other != NULL && names.base != NULL && mkdtemp(directory) != NULL;
    check_test_expect(test, ready, "x86 hard error unit memory and directory");
    uint32_t used = 0;
    uint32_t expected_used = 0;
    uint32_t calls = 0;
    uint32_t builtins = 0;
    uint32_t undecoded = 0;
    for (uint32_t i = 0; ready && i < BUSTER_BUILTIN_INTRINSIC_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
        const bool x86 = info->name_length > 15 && memcmp(info->name, "__builtin_ia32_", 15) == 0 &&
                         builtin_intrinsic_own_main_prototype_in_scope(info->name, info->name_length);
        BuiltinIntrinsicTypeResult types = { 0 };
        if (x86)
        {
            const BuiltinIntrinsicTypeInput input = {
                .encoding = info->encoding,
                .encoding_length = info->encoding_length,
                .attributes = info->attributes,
                .attributes_length = info->attributes_length,
                .custom_typechecking = info->custom_typechecking,
            };
            builtin_intrinsic_types_decode(table, &input, &types);
            undecoded += types.status != BUILTIN_INTRINSIC_TYPES_SUCCESS;
        }
        const uint64_t required = x86 && types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS ? types.ice_parameters | check_graph_x86_ice_mask(info, false) : 0;
        builtins += required != 0;
        for (uint32_t argument = 0; ready && argument < types.parameter_count && argument < 64; argument += 1)
        {
            if ((required & (UINT64_C(1) << argument)) != 0)
            {
                ready = used + 4096 < capacity && expected_used + 256 < capacity;
                const uint32_t line = calls + 1;
                uint32_t column = 0;
                if (ready)
                {
                    used += (uint32_t)snprintf(source + used, capacity - used, "void f%u(", line);
                    for (uint32_t p = 0; p < types.parameter_count; p += 1)
                    {
                        char name[16];
                        snprintf(name, sizeof(name), "p%u", p);
                        const String8 printed = type_print(table, &names, types.parameters[p], (String8){ .pointer = name, .length = strlen(name) }, 0, 0);
                        used += (uint32_t)snprintf(source + used, capacity - used, "%s%.*s", p != 0 ? "," : "", (int)printed.length, printed.pointer);
                        arena_pop_to(&names, 0);
                    }
                    const uint32_t start = used;
                    used += (uint32_t)snprintf(source + used, capacity - used, "){(void)");
                    uint32_t line_start = start;
                    while (line_start != 0 && source[line_start - 1] != '\n')
                    {
                        line_start -= 1;
                    }
                    column = used - line_start + 1;
                    used += (uint32_t)snprintf(source + used, capacity - used, "%.*s(", (int)info->name_length, info->name);
                    for (uint32_t p = 0; p < types.parameter_count; p += 1)
                    {
                        // The argument under test is its parameter; another constant argument takes a value its rules
                        // take (a rounding control's or SAE's 4, a scale's 1, a range's bottom), or 0.
                        uint32_t value = 0;
                        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(check_builtin_x86_rules); r += 1)
                        {
                            const CheckBuiltinX86Rule* rule = check_builtin_x86_rules + r;
                            if (rule->argument == p && check_builtin_x86_compare(info->name, info->name_length, rule) == 0)
                            {
                                value = rule->kind == CHECK_BUILTIN_X86_CONSTRAINT_RANGE ? rule->low : rule->kind == CHECK_BUILTIN_X86_CONSTRAINT_SCALE ? 1 : 4;
                            }
                        }
                        if (p == argument || (required & (UINT64_C(1) << p)) == 0)
                        {
                            used += (uint32_t)snprintf(source + used, capacity - used, "%sp%u", p != 0 ? "," : "", p);
                        }
                        else
                        {
                            used += (uint32_t)snprintf(source + used, capacity - used, "%s%u", p != 0 ? "," : "", value);
                        }
                    }
                    used += (uint32_t)snprintf(source + used, capacity - used, ");}\n");
                    expected_used += (uint32_t)snprintf(expected + expected_used, capacity - expected_used,
                                                        "%u:%u: error: argument to '%.*s' must be a constant integer\n", line, column,
                                                        (int)info->name_length, info->name);
                    calls += 1;
                }
            }
        }
    }
    snprintf(description, sizeof(description), "x86 hard error unit: %u calls of %u builtins (%u in scope don't decode)", calls, builtins, undecoded);
    check_test_expect(test, ready && calls != 0 && undecoded == 0, description);
    char path[256];
    char object[256];
    char output[256];
    char errors[256];
    snprintf(path, sizeof(path), "%s/every.c", directory);
    snprintf(object, sizeof(object), "%s/every.o", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    const bool written = ready && parse_test_write(path, source, used);
    if (written)
    {
        check_test_x86_hard_compare(test, clang, "every builtin's integer constant arguments", path, object, output, errors, expected, calls, text, other,
                                    capacity);
        printf("  x86 hard errors: %u nonconstant arguments of %u builtins against clang, by both paths with -fsyntax-only and -c; %u lowering rows' "
               "immediates and rounding controls among those arguments\n", calls, builtins, rows);
    }
    if (ready)
    {
        unlink(path);
        unlink(object);
        unlink(output);
        unlink(errors);
        rmdir(directory);
    }
    arena_release(&names);
    free(source);
    free(expected);
    free(text);
    free(other);
}

static void check_test_x86_diagnostics(CheckTest* test, TypeTable* types, const char* clang)
{
    check_test_x86_hard_errors(test, clang);
    check_test_x86_hard_every(test, types, clang);
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
        uint32_t hard_column; // The column of an expected hard-constraint error, clang's and both paths'.
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
        { "ICE stays hard in dead code", "unsigned f(V a,int n){if(0)return __builtin_ia32_cmpb128_mask(a,a,n,65535);return 0;}", 0, true, false, 35 },
        { "ICE stays hard in sizeof", "unsigned f(V a,int n){return sizeof(__builtin_ia32_cmpb128_mask(a,a,n,65535));}", 0, true, false, 37 },
        { "-w suppresses range", "unsigned f(V a){return BAD;}", 0, false, true },
        { "-w suppresses compound literal range", "unsigned f(V a){return (unsigned){BAD};}", 0, false, true },
        { "ICE stays hard with -w", "unsigned f(V a,int n){return __builtin_ia32_cmpb128_mask(a,a,n,65535);}", 0, true, true, 30 },
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
        if (fixtures[i].hard_column != 0)
        {
            check_test_x86_hard_diagnostic(test, errors, fixtures[i].name, "Clang", fixtures[i].hard_column);
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const int status = written ? check_test_packed_run(NULL, path, output, errors, scalar != 0, fixtures[i].quiet) : -1;
            const CheckX86TestDiagnostic actual = check_test_x86_diagnostic_read(errors, true);
            const bool same = status == oracle_status && actual.count == oracle.count &&
                              (actual.count == 0 || (actual.line == oracle.line && actual.column == oracle.column &&
                               actual.error == oracle.error && strcmp(actual.message, oracle.message) == 0));
            snprintf(description, sizeof(description), "x86 %s, scalar %u: status %d/ranges %u at %u:%u, Clang %d/%u at %u:%u: %.200s",
                     fixtures[i].name, scalar, status, actual.count, actual.line, actual.column, oracle_status, oracle.count,
                     oracle.line, oracle.column, actual.message);
            check_test_expect(test, same, description);
            if (fixtures[i].hard_column != 0)
            {
                check_test_x86_hard_diagnostic(test, errors, fixtures[i].name, scalar ? "scalar" : "vector", fixtures[i].hard_column);
            }
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
