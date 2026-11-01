// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source probes, checked against external Clang through the production diagnostic renderer in both
// schedules. Compare warning/error counts, complete packed-member messages and presumed line/column pairs.

typedef struct CheckPackedTestDiagnostic CheckPackedTestDiagnostic;
struct CheckPackedTestDiagnostic
{
    uint32_t line;
    uint32_t column;
    bool error;
    char message[512];
};

// The driver on `source`, by `executable` or in a child as a command line runs it, with `-fsyntax-only` and `-std=gnu17`, by
// the scalar path with `scalar`, and `options` (or none: count 0), its output and errors into `output` and `errors`. Returns
// its exit status.
static int check_test_packed_run_options(const char* executable, const char* source, const char* output, const char* errors, bool scalar, bool quiet,
                                         const char* const* options, uint32_t option_count)
{
    int result = -1;
    char* arguments[32];
    uint32_t count = 0;
    // The fixed arguments, the source and, for an external run, its NULL terminator.
    const uint32_t fixed = executable != NULL ? 7u : 5u;
    const bool fits = option_count <= BUSTER_ARRAY_LENGTH(arguments) - fixed;
    if (fits && executable != NULL)
    {
        arguments[count++] = (char*)executable;
    }
    arguments[count++] = scalar ? "--scalar" : "-fsyntax-only";
    arguments[count++] = quiet ? "-w" : "-fsyntax-only";
    arguments[count++] = "-fsyntax-only";
    arguments[count++] = "-std=gnu17";
    for (uint32_t i = 0; fits && i < option_count; i += 1)
    {
        arguments[count++] = (char*)options[i];
    }
    arguments[count++] = (char*)source;
    if (fits && executable != NULL)
    {
        arguments[count] = NULL;
        result = object_test_run(arguments, output, errors);
    }
    else if (fits)
    {
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
    }
    return result;
}

// One option's shorthand for the packed fixtures and #1085's range mappings; a null option adds none.
static int check_test_packed_run(const char* executable, const char* source, const char* output, const char* errors, bool scalar, const char* option)
{
    const char* options[1] = { option };
    return check_test_packed_run_options(executable, source, output, errors, scalar, false, options, option != NULL ? 1u : 0u);
}

static uint32_t check_test_packed_diagnostics(const char* path, CheckPackedTestDiagnostic* diagnostics, uint32_t capacity)
{
    FILE* file = fopen(path, "r");
    uint32_t count = 0;
    char* text = NULL;
    size_t allocated = 0;
    while (file != NULL && getline(&text, &allocated, file) >= 0)
    {
        char* message = strstr(text, "taking address of packed member");
        if (message != NULL)
        {
            CheckPackedTestDiagnostic diagnostic = { .error = strstr(text, "error: ") != NULL };
            const bool located = sscanf(text, "%*[^:]:%u:%u:", &diagnostic.line, &diagnostic.column) == 2;
            char* end = strstr(message, " [-W");
            end = end != NULL ? end : strchr(message, '\n');
            const size_t length = end != NULL ? (size_t)(end - message) : strlen(message);
            if (!located || count >= capacity || length >= sizeof(diagnostic.message))
            {
                count = UINT32_MAX;
                break;
            }
            memcpy(diagnostic.message, message, length);
            uint32_t place = count;
            while (place != 0 && (diagnostics[place - 1].line > diagnostic.line ||
                                 (diagnostics[place - 1].line == diagnostic.line && diagnostics[place - 1].column > diagnostic.column)))
            {
                diagnostics[place] = diagnostics[place - 1];
                place -= 1;
            }
            diagnostics[place] = diagnostic;
            count += 1;
        }
    }
    free(text);
    if (file != NULL)
    {
        fclose(file);
    }
    else
    {
        count = UINT32_MAX;
    }
    return count;
}

static void check_test_packed_intervals(CheckTest* test)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(NULL, page * 5, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = memory != MAP_FAILED && mprotect(memory + page, page, PROT_READ | PROT_WRITE) == 0 &&
                       mprotect(memory + page * 3, page, PROT_READ | PROT_WRITE) == 0;
    check_test_expect(test, ready, "packed interval guard allocation");
    for (uint32_t count = 0; ready && count <= 129; count += 1)
    {
        uint32_t* events = (uint32_t*)(memory + page * 2 - count * sizeof(uint32_t));
        uint8_t* analyzed = memory + page * 4 - count;
        const uint32_t starts[][3] = { { 0, 0, 0 }, { 1, 7, 15 }, { 0, 15, 31 }, { 1, 16, 31 } };
        const uint32_t ends[][3] = { { 0, 0, 0 }, { count != 0 ? count - 1 : 0, 17, 32 }, { 17, 32, 33 }, { 16, 31, 33 } };
        for (uint32_t pattern = 0; pattern < BUSTER_ARRAY_LENGTH(starts); pattern += 1)
        {
            memset(events, 0, (uint64_t)count * sizeof(uint32_t));
            for (uint32_t interval = 0; interval < 3; interval += 1)
            {
                const uint32_t first = starts[pattern][interval];
                const uint32_t end = ends[pattern][interval] < count ? ends[pattern][interval] : count;
                if (first < end)
                {
                    events[first] += 1;
                    if (end < count)
                    {
                        events[end] -= 1;
                    }
                }
            }
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                memset(analyzed - 1, 0xa5, (uint64_t)count + 1);
                check_input_packed_analyzed(events, analyzed, count, scalar != 0);
                bool same = analyzed[-1] == 0xa5;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    // Independent point membership, not another difference/prefix implementation.
                    bool expected = true;
                    for (uint32_t interval = 0; interval < 3; interval += 1)
                    {
                        expected &= !(i >= starts[pattern][interval] && i < ends[pattern][interval]);
                    }
                    same &= analyzed[i] == expected;
                }
                check_test_expect(test, same, "packed interval scan matches independent membership at guarded lane boundaries");
            }
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 5);
    }
}

static void check_test_packed_bounds(CheckTest* test)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(NULL, page * 5, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = memory != MAP_FAILED && mprotect(memory + page, page, PROT_READ | PROT_WRITE) == 0 &&
                       mprotect(memory + page * 3, page, PROT_READ | PROT_WRITE) == 0;
    check_test_expect(test, ready, "packed classifier guard allocation");
    for (uint32_t width = 0; ready && width <= 129; width += 1)
    {
        uint8_t* kinds = memory + page * 2 - width;
        uint8_t* roles = memory + page * 4 - width;
        for (uint32_t i = 0; i < width; i += 1)
        {
            kinds[i] = i % 3 == 0 ? PARSE_KIND_AMP : PARSE_KIND_IDENTIFIER;
            roles[i] = i % 5 == 0 ? PARSE_ROLE_OPERAND : PARSE_ROLE_PREFIX;
        }
        Parser parser = { .tokens = { .kinds = kinds, .count = width }, .tree = { .roles = roles } };
        for (uint32_t first = 0; first < width || (width == 0 && first == 0); first += 64)
        {
            const uint32_t count = width - first < 64 ? width - first : 64;
            uint64_t expected = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                expected |= (uint64_t)((first + i) % 3 == 0 && (first + i) % 5 != 0) << i;
            }
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                parser.options.scalar = scalar != 0;
                check_test_expect(test, check_input_packed_candidates(&parser, first, count) == expected,
                                  "packed classifiers match independent positions at guarded tails");
            }
        }
        for (uint32_t i = 0; i < width; i += 1)
        {
            kinds[i] = i % 3 == 0 ? 4 : 0;
        }
        for (uint32_t first = 0; first <= width; first += 1)
        {
            bool expected = false;
            for (uint32_t i = first; i < width; i += 1)
            {
                expected |= i % 3 == 0;
            }
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                check_test_expect(test, check_input_packed_any(kinds, first, width, 4, scalar != 0) == expected,
                                  "packed metadata scan respects the logical end and guard page");
            }
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 5);
    }
    const uint8_t packed[] = { 0, 1 };
    uint8_t kinds[] = { PARSE_KIND_AMP, PARSE_KIND_IDENTIFIER, PARSE_KIND_PERIOD, PARSE_KIND_IDENTIFIER };
    uint8_t roles[] = { PARSE_ROLE_PREFIX, PARSE_ROLE_OPERAND, PARSE_ROLE_MEMBER_OPERATOR, PARSE_ROLE_MEMBER };
    uint32_t ends[] = { 3, 1, 3, 3 };
    Parser parser = { .tokens = { .kinds = kinds, .count = 4 }, .tree = { .roles = roles } };
    TypeTable types = { .tags = { .packed = (uint8_t*)packed, .count = 2 } };
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        Arena arena = arena_reserve(4096);
        Arena scratch = arena_reserve(4096);
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        arena.capacity = scratch.capacity = 0;
        ends[0] = 1;
        parser.options.scalar = scalar != 0;
        CheckInput input = { 0 };
        CheckInputReader reader = { .output = &input, .parser = &parser, .types = &types, .arena = &arena, .scratch = &scratch, .ends = ends, .expression_limit = 4 };
        check_input_packed_members(&reader);
        check_test_expect(test, !input.resource && input.packed_member_count == 0 && input.packed_members == NULL &&
                              arena.length == 0 && scratch.length == 0,
                          "unused packed type plus ordinary address needs no diagnostic storage or scratch");
        arena.capacity = arena_capacity;
        scratch.capacity = scratch_capacity;
        arena_release(&scratch);
        arena_release(&arena);
    }
    ends[0] = 3;
    for (uint32_t failure = 0; failure < 2; failure += 1)
    {
        Arena arena = arena_reserve(4096);
        Arena scratch = arena_reserve(4096);
        const uint64_t arena_capacity = arena.capacity;
        const uint64_t scratch_capacity = scratch.capacity;
        arena.capacity = failure == 0 ? 0 : arena.capacity;
        scratch.capacity = failure == 1 ? 0 : scratch.capacity;
        CheckInput input = { 0 };
        CheckInputReader reader = { .output = &input, .parser = &parser, .types = &types, .arena = &arena, .scratch = &scratch, .ends = ends, .expression_limit = 4 };
        if (failure == 0)
        {
            check_input_packed_storage(&reader, 1);
        }
        else
        {
            check_input_packed_members(&reader);
        }
        check_test_expect(test, input.resource && input.packed_member_count == 0 && scratch.length == 0,
                          "packed witness allocation failure is explicit and restores scratch");
        arena.capacity = arena_capacity;
        scratch.capacity = 0;
        input = (CheckInput){ .packed_member_count = 1, .graph = { .count = 1 } };
        check_test_expect(test, !check_input_packed_finalize(&input, &parser, &types, NULL, &scratch) && input.resource && scratch.length == 0,
                          "packed conversion-context allocation failure is explicit");
        for (uint32_t limit = 0; limit < (parser.tokens.count + 1) * sizeof(uint32_t) + parser.tokens.count; limit += 1)
        {
            scratch.length = 0;
            scratch.capacity = limit;
            input = (CheckInput){ 0 };
            check_input_packed_contexts(&reader);
            check_test_expect(test, input.resource, "packed lexical-context handoff refuses every partial allocation");
        }
        for (uint32_t limit = 0; limit < 5 * sizeof(uint32_t) * types.tags.count; limit += 1)
        {
            scratch.length = 0;
            scratch.capacity = limit;
            input = (CheckInput){ 0 };
            CheckInputPackedNames names = { 0 };
            check_test_expect(test, !check_input_packed_names(&reader, &names) && input.resource &&
                                  !check_input_packed_names(&reader, &names),
                              "anonymous-name handoff refuses every partial allocation and a repeated query");
        }
        scratch.capacity = scratch_capacity;
        arena_release(&scratch);
        arena_release(&arena);
    }
}

static void check_test_packed_members(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_packed_bounds(test);
    check_test_packed_intervals(test);
    const char* prefix =
        "struct __attribute__((packed)) P { char c; int w; int a[2]; };\n"
        "struct __attribute__((packed)) F { int w; };\n"
        "struct __attribute__((packed,aligned(4))) A { int first; char c; int w; };\n"
        "struct Q { char c; int w __attribute__((packed)); };\n"
        "struct N { struct P p; };\n"
        "struct __attribute__((packed)) O { char c; struct { int x; int y; } n; };\n"
        "struct Plain { char c; int w; };\n"
        "struct P *get(void); void take(void*); void itake(int*); void* give(void*);\n";
    const struct
    {
        const char* source;
        uint32_t warnings;
        bool rejected;
    } fixtures[] =
    {
        { "int*f(struct P*p){return &p->w;}\n", 1 },
        { "int*f(struct P*p){return &(p->w);}\n", 1 },
        { "int*f(struct P*p){return &((p)->w);}\n", 1 },
        { "int*f(struct P*p){return &__extension__ p->w;}\n", 1 },
        { "char*f(struct P*p){return &p->c;}\n", 0 },
        { "int*f(struct Plain*p){return &p->w;}\n", 0 },
        { "int*f(struct F*p){return &p->w;}\n", 1 },
        { "int*f(struct A*p){return &p->first;}\n", 0 },
        { "int*f(struct A*p){return &p->w;}\n", 1 },
        { "int*f(struct Q*p){return &p->w;}\n", 1 },
        { "int*f(struct N*p){return &p->p.w;}\n", 1 },
        { "int*f(struct O*p){return &p->n.y;}\n", 1 },
        { "void*f(struct P*p){return &p->w;}\n", 0 },
        { "void f(struct P*p){take(&p->w);}\n", 0 },
        { "void f(struct P*p){itake(&p->w);}\n", 1 },
        { "char*f(struct P*p){return (char*)&p->w;}\n", 0 },
        { "short*f(struct P*p){return (short*)&p->w;}\n", 1 },
        { "long f(struct P*p){return (long)&p->w;}\n", 0 },
        { "void f(struct P*p){(void)&p->w;}\n", 1 },
        { "int*f(struct A*p){return (int*)&p->w;}\n", 0 },
        { "int*f(struct P*p){return &get()->w;}\n", 0 },
        { "int*f(struct P*p){return &(*p).w;}\n", 0 },
        { "int*f(struct P*p){return &p[0].w;}\n", 0 },
        { "int*f(struct P*p){return &p->a[0];}\n", 0 },
        { "int*f(struct P*p){return p->a;}\n", 0 },
        { "int(*f(struct P*p))[2]{return &p->a;}\n", 1 },
        { "void f(struct P*p){__builtin_memcpy(&p->w,&p->a,4);}\n", 0 },
        { "void*f(struct P*p){return __builtin_assume_aligned(&p->w,1);}\n", 1 },
        { "void f(struct P*p){__builtin_prefetch(&p->w);}\n", 0 },
        { "unsigned long f(struct P*p){return __builtin_object_size(&p->w,0);}\n", 0 },
        { "int f(struct P*p){return __atomic_load_n(&p->w,0);}\n", 1 },
        { "int*f(struct P*p){return 0?&p->w:(void*)0;}\n", 1 },
        { "void*f(struct P*p){return 0?&p->w:(void*)0;}\n", 0 },
        { "void*f(struct P*p){return (0,&p->w);}\n", 0 },
        { "void*f(struct P*p,int n){return n?&p->w:0;}\n", 0 },
        { "void*f(struct P*p,int n){return (void*)(n?&p->w:0);}\n", 1 },
        { "void*f(struct P*p){return (void*)(0,&p->w);}\n", 1 },
        { "long f(struct P*p,int n){return (long)(n?&p->w:0);}\n", 1 },
        { "unsigned long f(struct P*p){return sizeof((void*)(0,&p->w));}\n", 1 },
        { "void f(struct P*p){(void)(&p->w,0);}\n", 1 },
        { "unsigned long f(struct P*p){return sizeof(&p->w);}\n", 1 },
        { "unsigned long f(struct P*p){return sizeof((char*)&p->w);}\n", 0 },
        { "void f(struct P*p){__typeof__(&p->w) v;}\n", 1 },
        { "void f(struct P*p){__typeof__((void*)&p->w) v;}\n", 0 },
        { "unsigned long f(struct P*p){return sizeof(take(&p->w));}\n", 1 },
        { "unsigned long f(struct P*p){return __alignof__(give(&p->w));}\n", 1 },
        { "unsigned long f(struct P*p){return sizeof(__builtin_memcpy(&p->w,0,4));}\n", 1 },
        { "unsigned long f(struct P*p,int n){return sizeof(int[(take(&p->w),n)]);}\n", 1 },
        { "void f(struct P*p){__typeof__(give(&p->w)) v;}\n", 1 },
        { "int f(struct P*p){return _Generic(take(&p->w),default:0);}\n", 1 },
        { "int f(struct P*p){return __builtin_choose_expr(0,(take(&p->w),1),0);}\n", 1 },
        { "int f(struct P*p){return __builtin_choose_expr(1,(take(&p->w),1),0);}\n", 0 },
        { "int f(struct P*p){return _Generic(0,int:0,default:(take(&p->w),1));}\n", 1 },
        { "int f(struct P*p){return _Generic(0,int:(take(&p->w),1),default:0);}\n", 0 },
        { "int f(struct P*p){return 0?(take(&p->w),1):0;}\n", 0 },
        { "int f(struct P*p){return __builtin_constant_p(take(&p->w));}\n", 0 },
        { "unsigned long f(struct P*p){return __builtin_object_size(give(&p->w),0);}\n", 0 },
        { "int*f(struct P*p){return &_Generic(0,int:p->w);}\n", 1 },
        { "int*f(struct P*p){return &_Generic(&p->w,int*:p->w);}\n", 2 },
        { "int*f(struct P*p){return &__builtin_choose_expr(1,p->w,p->w);}\n", 1 },
        { "void*f(struct P*p){return _Generic(0,int:&p->w);}\n", 0 },
        { "void*f(struct P*p){return __builtin_choose_expr(1,&p->w,(int*)0);}\n", 0 },
        { "char*f(struct P*p){return (char*)_Generic(0,int:&p->w);}\n", 0 },
        { "char*f(struct P*p){return (char*)__builtin_choose_expr(1,&p->w,(int*)0);}\n", 0 },
        { "unsigned long f(struct P*p){return sizeof((char*)_Generic(0,int:&p->w));}\n", 0 },
        { "unsigned long f(struct P*p){return sizeof((char*)__builtin_choose_expr(1,&p->w,(int*)0));}\n", 0 },
        { "unsigned long f(struct P*p){return sizeof(_Generic(0,int:(take(&p->w),1)));}\n", 1 },
        { "struct F g; int*f(void){return &g.w;}\n", 1 },
        { "struct F g __attribute__((aligned(4))); int*f(void){return &g.w;}\n", 0 },
        { "struct P g __attribute__((aligned(4))); int*f(void){return &g.w;}\n", 1 },
        { "typedef struct F AF __attribute__((aligned(4))); AF g; int*f(void){return &g.w;}\n", 0 },
        { "#pragma pack(1)\nstruct K{char c;int w;};\n#pragma pack()\nint*f(struct K*p){return &p->w;}\n", 0 },
        { "union __attribute__((packed)) U{char c;int w;}; int*f(union U*p){return &p->w;}\n", 1 },
        { "struct __attribute__((packed)) R {char c; struct { int x; };}; int*f(struct R*p){return &p->x;}\n", 1 },
        { "struct R {struct __attribute__((packed)){char c;int w;};}; int*f(struct R*p){return &p->w;}\n", 1 },
        { "typedef struct __attribute__((packed)){char c;int w;} T; int*f(T*p){return &p->w;}\n", 1 },
        { "typedef struct{struct __attribute__((packed)){char c;int w;} n;} T; int*f(T*p){return &p->n.w;}\n", 1 },
        { "typedef struct{struct __attribute__((packed)){char c;int w;} n;} "
          "LongTypedefNameForAnAnonymousParentRecordWhoseDiagnosticCapacityMustIncludeTheWholeIdentifierRatherThanOnlyTheMissingTagNameAndItsSeparators; "
          "int*f(LongTypedefNameForAnAnonymousParentRecordWhoseDiagnosticCapacityMustIncludeTheWholeIdentifierRatherThanOnlyTheMissingTagNameAndItsSeparators*p){return &p->n.w;}\n", 1 },
        { "typedef struct __attribute__((packed)){struct{int w;};} T; int*f(T*p){return &p->w;}\n", 1 },
        { "struct __attribute__((packed)){char c;int w;} a; int*f(void){return &a.w;}\n", 1 },
        { "struct R{struct{struct __attribute__((packed)){char c;int w;} n;} m;}; int*f(struct R*p){return &p->m.n.w;}\n", 1 },
        { "struct R{union __attribute__((packed)){char c;int w;};}; int*f(struct R*p){return &p->w;}\n", 1 },
        { "#define ANON struct __attribute__((packed)){char c;int w;}\nANON a; int*f(void){return &a.w;}\n", 1 },
        { "struct H{struct P*next;}; int*f(struct H*p){return &p->next->w;}\n", 0 },
        { "struct Incomplete; struct Incomplete*f(struct P*p){return (struct Incomplete*)&p->w;}\n", 0 },
        { "void f(struct P*p){int*v=&p->w;}\n", 1 },
        { "void f(struct P*p){void*v=&p->w;}\n", 0 },
        { "void f(struct P*p){int*v;v=&p->w;}\n", 1 },
        { "void f(struct P*p){void*v;v=&p->w;}\n", 0 },
        { "int f(struct P*p,void*q){return &p->w==q;}\n", 1 },
        { "int f(struct P*p){if(&p->w)return 1;return 0;}\n", 0 },
        { "int f(struct P*p){return !!&p->w;}\n", 0 },
        { "int f(struct P*p){return __builtin_constant_p(&p->w);}\n", 1 },
        { "int*f(struct P*p){return __builtin_choose_expr(0,(int*)0,&p->w);}\n", 1 },
        { "int*f(struct P*p){return _Generic(0,int:&p->w);}\n", 1 },
        { "enum{VALUE=_Generic(1,int:7,default:1/0)}; _Static_assert(VALUE==7,\"generic\"); int*f(struct P*p){return &p->w;}\n", 1 },
        { "#pragma GCC diagnostic ignored \"-Waddress-of-packed-member\"\nint*f(struct P*p){return &p->w;}\n", 0 },
        { "#pragma GCC diagnostic error \"-Waddress-of-packed-member\"\nint*f(struct P*p){return &p->w;}\n", 1, true },
        { "#pragma GCC diagnostic ignored \"-Weverything\"\n#pragma GCC diagnostic warning \"-Waddress-of-packed-member\"\nint*f(struct P*p){return &p->w;}\n", 1 },
        { "#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Waddress-of-packed-member\"\nint*f(struct P*p){return &p->w;}\n#pragma GCC diagnostic pop\nint*g(struct P*p){return &p->w;}\n", 1 },
        { "#define ADDR(p) (&(p)->w)\nint*f(struct P*p){return ADDR(p);}\n", 1 },
        { "int*f(struct P*p){return &p->w;}\nthis is invalid;\n", 1, true },
        { "void f(struct P*p,int n){\n#pragma clang loop unroll(full) unroll_count(4)\nwhile(n--)itake(&p->w);\n}\n", 1, true },
        { "void f(struct P*p,int n){\n#pragma clang loop unroll(full) unroll_count(4)\nwhile(n--)take(&p->w);\n}\n", 0, true },
    };
    char path[1024];
    char output[1024];
    char errors[1024];
    snprintf(path, sizeof(path), "%s/packed.c", directory);
    snprintf(output, sizeof(output), "%s/packed.out", directory);
    snprintf(errors, sizeof(errors), "%s/packed.err", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[8192];
        const int length = snprintf(source, sizeof(source), "%s%s", prefix, fixtures[i].source);
        const bool written = length > 0 && (size_t)length < sizeof(source) && parse_test_write(path, source, (size_t)length);
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", "-Wno-unused-value", path, NULL };
        const int oracle_status = written ? object_test_run(arguments, output, errors) : -1;
        CheckPackedTestDiagnostic oracle[16] = { 0 };
        const uint32_t oracle_count = check_test_packed_diagnostics(errors, oracle, BUSTER_ARRAY_LENGTH(oracle));
        char description[320];
        snprintf(description, sizeof(description), "packed fixture %u: Clang warns %u times (expected %u), status %d", i, oracle_count, fixtures[i].warnings, oracle_status);
        check_test_expect(test, oracle_status == (fixtures[i].rejected ? 1 : 0) && oracle_count == fixtures[i].warnings, description);
        for (uint32_t route = 0; route < (executable != NULL ? 4u : 2u); route += 1)
        {
            const int status = written ? check_test_packed_run(route >= 2 ? executable : NULL, path, output, errors, (route & 1) != 0, NULL) : -1;
            CheckPackedTestDiagnostic actual[16] = { 0 };
            const uint32_t count = check_test_packed_diagnostics(errors, actual, BUSTER_ARRAY_LENGTH(actual));
            snprintf(description, sizeof(description), "packed fixture %u, route %u: %u diagnostics/status %d, Clang %u/%d", i, route, count, status, oracle_count, oracle_status);
            check_test_expect(test, status == oracle_status && count == oracle_count, description);
            for (uint32_t item = 0; count == oracle_count && item < count && item < BUSTER_ARRAY_LENGTH(actual); item += 1)
            {
                snprintf(description, sizeof(description), "packed fixture %u, route %u: warning at %u:%u, Clang %u:%u: %.150s", i, route,
                         actual[item].line, actual[item].column, oracle[item].line, oracle[item].column, actual[item].message);
                check_test_expect(test, actual[item].line == oracle[item].line && actual[item].column == oracle[item].column &&
                                      actual[item].error == oracle[item].error && strcmp(actual[item].message, oracle[item].message) == 0, description);
            }
        }
    }
    unlink(errors);
    unlink(output);
    unlink(path);
}
