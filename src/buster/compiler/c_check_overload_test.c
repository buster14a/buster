// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original declaration, selection and retained-identity tests; independent C sources use Clang as an oracle.
#ifndef BUSTER_C_CHECK_OVERLOAD_TEST_INCLUDED
#define BUSTER_C_CHECK_OVERLOAD_TEST_INCLUDED

typedef struct CheckOverloadFixture CheckOverloadFixture;
struct CheckOverloadFixture
{
    const char* name;
    const char* source;
    TypeId parameters[3][3];
    uint8_t parameter_count[3];
    uint8_t variadic;
    uint8_t unavailable;
    uint8_t count;
    CheckValue arguments[3];
    uint32_t argument_count;
    CheckOverloadStatus status;
    uint32_t selected;
    CheckDiagnostic diagnostic;
};

static void check_test_overload_ranks(CheckTest* test)
{
    const uint32_t lengths[] = { 0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 257 };
    uint8_t* pages = mmap(0, 4 * BUSTER_ARENA_PAGE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = pages != MAP_FAILED && mprotect(pages + BUSTER_ARENA_PAGE_BYTES, BUSTER_ARENA_PAGE_BYTES, PROT_NONE) == 0 &&
                       mprotect(pages + 3 * BUSTER_ARENA_PAGE_BYTES, BUSTER_ARENA_PAGE_BYTES, PROT_NONE) == 0;
    check_test_expect(test, ready, "overload conversion rows have guard pages");
    for (uint32_t size = 0; ready && size < BUSTER_ARRAY_LENGTH(lengths); size += 1)
    {
        const uint32_t count = lengths[size];
        uint8_t* left = pages + BUSTER_ARENA_PAGE_BYTES - count;
        uint8_t* right = pages + 3 * BUSTER_ARENA_PAGE_BYTES - count;
        for (uint32_t sample = 0; sample < 65; sample += 1)
        {
            uint32_t bad = 0;
            uint32_t ranks = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                left[i] = check_test_random(test) % (CHECK_OVERLOAD_ELLIPSIS + 1);
                right[i] = sample == 0 ? left[i] : check_test_random(test) % (CHECK_OVERLOAD_ELLIPSIS + 1);
                bad |= left[i] != CHECK_OVERLOAD_BAD_POINTER && right[i] == CHECK_OVERLOAD_BAD_POINTER ? 1u : 0u;
                bad |= left[i] == CHECK_OVERLOAD_BAD_POINTER && right[i] != CHECK_OVERLOAD_BAD_POINTER ? 2u : 0u;
                ranks |= left[i] < right[i] ? 1u : 0u;
                ranks |= left[i] > right[i] ? 2u : 0u;
            }
            const uint32_t expected = bad == 0 ? ranks : bad | 4u;
            check_test_expect(test, check_overload_rank_order(left, right, count, true) == expected &&
                                   check_overload_rank_order(left, right, count, false) == expected,
                              "overload rank partial order agrees on scalar/vector guarded tails");
        }
    }
    if (pages != MAP_FAILED)
    {
        munmap(pages, 4 * BUSTER_ARENA_PAGE_BYTES);
    }
}

static void check_test_overload_declarations(CheckTest* test, TypeTable* table)
{
    const TypeId integer = BUSTER_TYPE_INT;
    const TypeId floating = BUSTER_TYPE_DOUBLE;
    const TypeId fi = type_function(table, integer, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId fd = type_function(table, floating, &floating, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId wrong_return = type_function(table, floating, &integer, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
    CheckOverloadCandidate candidates[4] = {
        { .type = fi, .declaration = 10, .canonical = 10, .object = 100, .previous = UINT32_MAX, .scope = 1, .position = 11, .flags = BUSTER_CHECK_OVERLOADABLE },
        { .type = fd, .declaration = 20, .canonical = 20, .object = 200, .previous = 0, .scope = 1, .position = 21, .flags = BUSTER_CHECK_OVERLOADABLE },
    };
    const CheckOverloadContext context = { .source_position = 100 };
    CheckOverloadCandidate next = { .type = fi, .declaration = 30, .canonical = 30, .object = 300, .previous = 1, .scope = 1, .position = 31, .flags = BUSTER_CHECK_OVERLOADABLE };
    CheckOverloadResult result = check_overload_declare(table, candidates, 2, 1, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.canonical == 10 && result.object == 100 && result.candidate == 0,
                      "same signature redeclaration preserves exact canonical object identity");
    next.type = wrong_return;
    result = check_overload_declare(table, candidates, 2, 1, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_CONFLICT, "return type cannot create another overload");
    next.type = fi;
    next.flags = 0;
    result = check_overload_declare(table, candidates, 2, 1, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_ATTRIBUTE, "redeclaration retains overloadable attribute");
    candidates[0].flags = 0;
    next.type = fd;
    result = check_overload_declare(table, candidates, 1, 0, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_ATTRIBUTE, "only one unmarked overload signature is allowed");
    next.flags = BUSTER_CHECK_OVERLOADABLE;
    result = check_overload_declare(table, candidates, 1, 0, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.canonical == 30, "one unmarked function coexists with marked signatures");
    next.type = type_function(table, integer, 0, 0, 0);
    result = check_overload_declare(table, candidates, 0, UINT32_MAX, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_PROTOTYPE, "marked function requires a prototype");
    next.type = fi;
    candidates[0].flags = BUSTER_CHECK_OVERLOADABLE | BUSTER_CHECK_OVERLOAD_DEFINED | BUSTER_CHECK_OVERLOAD_UNAVAILABLE;
    result = check_overload_declare(table, candidates, 1, 0, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.flags == candidates[0].flags, "unavailable and definition state survive redeclaration");
    next.flags |= BUSTER_CHECK_OVERLOAD_DEFINED;
    result = check_overload_declare(table, candidates, 1, 0, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_REDEFINITION, "duplicate overload definition is rejected");
    next.scope = 2;
    next.flags = BUSTER_CHECK_OVERLOADABLE;
    result = check_overload_declare(table, candidates, 1, 0, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_INVALID, "source must reset an overload chain at a new lexical scope");
    next.canonical = 10;
    next.object = 100;
    result = check_overload_declare(table, candidates, 1, UINT32_MAX, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.canonical == 10, "scope reset preserves separately resolved external linkage identity");
    candidates[0].previous = 0;
    next.scope = 1;
    result = check_overload_declare(table, candidates, 1, 0, &next, &context);
    check_test_expect(test, result.status == CHECK_OVERLOAD_INVALID, "cyclic declaration chain fails before traversal");
}

static void check_test_overload_selection(CheckTest* test, TypeTable* table, const char* clang)
{
    const TypeId ip = type_pointer(table, BUSTER_TYPE_INT);
    const TypeId cp = type_pointer(table, BUSTER_TYPE_CHAR);
    const TypeId vp = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeId cip = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    const TypeId vip = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_VOLATILE));
    const TypeId cvip = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE));
    const TypeId ipp = type_pointer(table, ip);
    const TypeId qualified = type_pointer(table, type_qualify(table, cip, BUSTER_TYPE_CONST));
    const TypeId vector = type_vector(table, BUSTER_TYPE_INT, 4, true);
    const TypeId gnu_vector = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 133901);
    const bool complete = type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0);
    check_test_expect(test, complete, "overload nominal enum fixture");
    const TypeId ep = type_pointer(table, enumeration);
    const TypeId up = type_pointer(table, BUSTER_TYPE_UINT);
    const TypeId returns[3] = { BUSTER_TYPE_INT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG };
    const CheckOverloadFixture fixtures[] = {
        { .name = "exact integer overload", .source = "int f(int) O; double f(double) O; CHECK(f(3),int);", .parameters = {{ BUSTER_TYPE_INT },{ BUSTER_TYPE_DOUBLE }}, .parameter_count = {1,1}, .count = 2, .arguments = {{.type=BUSTER_TYPE_INT}}, .argument_count = 1, .selected = 0 },
        { .name = "exact floating overload", .source = "int f(int) O; double f(double) O; CHECK(f(3.0),double);", .parameters = {{ BUSTER_TYPE_INT },{ BUSTER_TYPE_DOUBLE }}, .parameter_count = {1,1}, .count = 2, .arguments = {{.type=BUSTER_TYPE_DOUBLE}}, .argument_count = 1, .selected = 1 },
        { .name = "promotion beats integer conversion", .source = "int f(int) O; double f(long) O; CHECK(f((short)3),int);", .parameters = {{ BUSTER_TYPE_INT },{ BUSTER_TYPE_LONG }}, .parameter_count = {1,1}, .count = 2, .arguments = {{.type=BUSTER_TYPE_SHORT}}, .argument_count = 1, .selected = 0 },
        { .name = "equal floating promotions stay ambiguous", .source = "int f(double) O; double f(long double) O; void g(void){f(1.0f);}", .parameters = {{ BUSTER_TYPE_DOUBLE },{ BUSTER_TYPE_LONG_DOUBLE }}, .parameter_count = {1,1}, .count = 2, .arguments = {{.type=BUSTER_TYPE_FLOAT}}, .argument_count = 1, .status = CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "float128 promotion", .source = "int f(__float128) O; double f(int) O; CHECK(f(1.0),int);", .parameters = {{ BUSTER_TYPE_FLOAT128 },{ BUSTER_TYPE_INT }}, .parameter_count = {1,1}, .count = 2, .arguments = {{.type=BUSTER_TYPE_DOUBLE}}, .argument_count = 1, .selected = 0 },
        { .name = "argument partial order is not rank sum", .source = "int f(int,long,long) O; double f(long,int,int) O; void g(void){f(1,1,1);}", .parameters = {{BUSTER_TYPE_INT,BUSTER_TYPE_LONG,BUSTER_TYPE_LONG},{BUSTER_TYPE_LONG,BUSTER_TYPE_INT,BUSTER_TYPE_INT}}, .parameter_count={3,3}, .count=2, .arguments={{.type=BUSTER_TYPE_INT},{.type=BUSTER_TYPE_INT},{.type=BUSTER_TYPE_INT}}, .argument_count=3, .status=CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "unavailable fallback loses to exact", .source = "int f(...) O __attribute__((unavailable)); double f(int) O; CHECK(f(1),double);", .parameters={{0},{BUSTER_TYPE_INT}}, .parameter_count={0,1}, .variadic=1, .unavailable=1, .count=2, .arguments={{.type=BUSTER_TYPE_INT}}, .argument_count=1, .selected=1 },
        { .name = "selected unavailable fallback rejects", .source = "int f(...) O __attribute__((unavailable)); double f(int) O; void g(void){f((void*)1);}", .parameters={{0},{BUSTER_TYPE_INT}}, .parameter_count={0,1}, .variadic=1, .unavailable=1, .count=2, .arguments={{.type=vp}}, .argument_count=1, .status=CHECK_OVERLOAD_UNAVAILABLE, .selected=0 },
        { .name = "argument arity no match", .source = "int f(int) O; void g(void){f();}", .parameters={{BUSTER_TYPE_INT}}, .parameter_count={1}, .count=1, .status=CHECK_OVERLOAD_NO_MATCH },
        { .name = "pointer qualification subset", .source = "int f(const int*) O; double f(const volatile int*) O; CHECK(f((int*)1),int);", .parameters={{cip},{cvip}}, .parameter_count={1,1}, .count=2, .arguments={{.type=ip}}, .argument_count=1, .selected=0 },
        { .name = "pointer qualifications incomparable", .source = "int f(const int*) O; double f(volatile int*) O; void g(int*p){f(p);}", .parameters={{cip},{vip}}, .parameter_count={1,1}, .count=2, .arguments={{.type=ip}}, .argument_count=1, .status=CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "multilevel qualification is certified", .source = "int f(const int*const*) O; double f(void*) O; CHECK(f((int**)1),int);", .parameters={{qualified},{vp}}, .parameter_count={1,1}, .count=2, .arguments={{.type=ipp,.category=CHECK_LVALUE}}, .argument_count=1, .selected=0 },
        { .name = "void conversion beats bool", .source = "int f(void*) O; double f(_Bool) O; CHECK(f((int*)1),int);", .parameters={{vp},{BUSTER_TYPE_BOOL}}, .parameter_count={1,1}, .count=2, .arguments={{.type=ip}}, .argument_count=1, .selected=0 },
        { .name = "C void reverse conversion loses to bool", .source = "int f(int*) O; double f(_Bool) O; CHECK(f((void*)1),double);", .parameters={{ip},{BUSTER_TYPE_BOOL}}, .parameter_count={1,1}, .count=2, .arguments={{.type=vp}}, .argument_count=1, .selected=1 },
        { .name = "C void reverse conversion beats ellipsis", .source = "int f(int*) O; double f(...) O; CHECK(f((void*)1),int);", .parameters={{ip},{0}}, .parameter_count={1,0}, .variadic=2, .count=2, .arguments={{.type=vp}}, .argument_count=1, .selected=0 },
        { .name = "bad pointer loses to ellipsis", .source = "int f(int*) O; double f(...) O; CHECK(f((char*)1),double);", .parameters={{ip},{0}}, .parameter_count={1,0}, .variadic=2, .count=2, .arguments={{.type=cp}}, .argument_count=1, .selected=1 },
        { .name = "selected incompatible pointer still diagnoses", .source = "int f(int*) O; void g(char*p){f(p);}", .parameters={{ip}}, .parameter_count={1}, .count=1, .arguments={{.type=cp}}, .argument_count=1, .selected=0, .diagnostic=CHECK_DIAG_INCOMPATIBLE },
        { .name = "compatible pointer beats void", .source = "enum E{A}; int f(enum E*) O; double f(void*) O; CHECK(f((unsigned*)1),int);", .parameters={{ep},{vp}}, .parameter_count={1,1}, .count=2, .arguments={{.type=up}}, .argument_count=1, .selected=0 },
        { .name = "enum canonical type beats compatible integer", .source = "enum E{A}; int f(enum E) O; double f(unsigned) O; CHECK(f((enum E)0),int);", .parameters={{enumeration},{BUSTER_TYPE_UINT}}, .parameter_count={1,1}, .count=2, .arguments={{.type=enumeration}}, .argument_count=1, .selected=0 },
        { .name = "enum underlying promotion", .source = "enum E{A}; int f(int) O; double f(unsigned) O; CHECK(f((enum E)0),double);", .parameters={{BUSTER_TYPE_INT},{BUSTER_TYPE_UINT}}, .parameter_count={1,1}, .count=2, .arguments={{.type=enumeration}}, .argument_count=1, .selected=1 },
        { .name = "complex promotion beats real conversion", .source = "int f(double _Complex) O; double f(float) O; CHECK(f((float _Complex)0),int);", .parameters={{BUSTER_TYPE_COMPLEX_DOUBLE},{BUSTER_TYPE_FLOAT}}, .parameter_count={1,1}, .count=2, .arguments={{.type=BUSTER_TYPE_COMPLEX_FLOAT}}, .argument_count=1, .selected=0 },
        { .name = "complex equal promotions ambiguous", .source = "int f(double _Complex) O; double f(long double _Complex) O; void g(void){f((float _Complex)0);}", .parameters={{BUSTER_TYPE_COMPLEX_DOUBLE},{BUSTER_TYPE_COMPLEX_LONG_DOUBLE}}, .parameter_count={1,1}, .count=2, .arguments={{.type=BUSTER_TYPE_COMPLEX_FLOAT}}, .argument_count=1, .status=CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "real conversion beats complex real", .source = "int f(double _Complex) O; double f(long) O; CHECK(f(1.0f),double);", .parameters={{BUSTER_TYPE_COMPLEX_DOUBLE},{BUSTER_TYPE_LONG}}, .parameter_count={1,1}, .count=2, .arguments={{.type=BUSTER_TYPE_FLOAT}}, .argument_count=1, .selected=1 },
        { .name = "ext vector scalar splat", .source = "typedef int V __attribute__((ext_vector_type(4))); int f(V) O; double f(...) O; CHECK(f(1),int);", .parameters={{vector},{0}}, .parameter_count={1,0}, .variadic=2, .count=2, .arguments={{.type=BUSTER_TYPE_INT}}, .argument_count=1, .selected=0 },
        { .name = "GNU vector has no scalar splat", .source = "typedef int V __attribute__((vector_size(16))); int f(V) O; double f(...) O; CHECK(f(1),double);", .parameters={{gnu_vector},{0}}, .parameter_count={1,0}, .variadic=2, .count=2, .arguments={{.type=BUSTER_TYPE_INT}}, .argument_count=1, .selected=1 },
        { .name = "zero argument overload tie", .source = "int f(void) O; double f(...) O; void g(void){f();}", .parameter_count={0,0}, .variadic=2, .count=2, .status=CHECK_OVERLOAD_AMBIGUOUS },
        { .name = "unused ellipsis does not break ties", .source = "int f(int) O; double f(int,...) O; void g(void){f(1);}", .parameters={{BUSTER_TYPE_INT},{BUSTER_TYPE_INT}}, .parameter_count={1,1}, .variadic=2, .count=2, .arguments={{.type=BUSTER_TYPE_INT}}, .argument_count=1, .status=CHECK_OVERLOAD_AMBIGUOUS },
    };
    Arena scratch = arena_reserve(1u << 20);
    char directory[] = "/tmp/buster-check-overload-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        char source[2048];
        const int length = snprintf(source, sizeof(source), "#define O __attribute__((overloadable))\n#define CHECK(e,t) _Static_assert(_Generic((e),t:1,default:0),\"selected type\")\n%s\n", fixtures[i].source);
        ready &= length > 0 && (uint32_t)length < sizeof(source) && parse_test_write(paths[i], source, (uint32_t)length);
    }
    const char* mode = "-std=gnu17";
    const bool oracle = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, oracle, "independent overload ranking Clang oracle runs");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const CheckOverloadFixture* fixture = fixtures + i;
        CheckOverloadCandidate candidates[3] = { 0 };
        for (uint32_t c = 0; c < fixture->count; c += 1)
        {
            candidates[c] = (CheckOverloadCandidate){
                .type = type_function(table, returns[c], fixture->parameters[c], fixture->parameter_count[c], BUSTER_TYPE_FLAG_PROTOTYPE | ((fixture->variadic >> c) & 1)),
                .declaration = 10 + c * 10,
                .canonical = 10 + c * 10,
                .object = 100 + c,
                .previous = c == 0 ? UINT32_MAX : c - 1,
                .scope = 1,
                .position = 11 + c * 10,
                .flags = BUSTER_CHECK_OVERLOADABLE | (((fixture->unavailable >> c) & 1) != 0 ? BUSTER_CHECK_OVERLOAD_UNAVAILABLE : 0),
            };
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            CheckConversion conversions[3] = { 0 };
            CheckAction action = {
                .op = CHECK_OP_CALL,
                .operands = {{ .type = candidates[fixture->count - 1].type, .category = CHECK_FUNCTION }},
                .arguments = fixture->arguments,
                .argument_count = fixture->argument_count,
                .argument_conversions = conversions,
                .extensions = BUSTER_CHECK_EXTENSION_GNU | BUSTER_CHECK_EXTENSION_GNU_CONSTANT,
                .source_position = 100,
            };
            CheckActionResult checked;
            const CheckOverloadResult result = check_overload_call(table, &action, candidates, fixture->count, fixture->count - 1, &scratch, path != 0, &checked, 0);
            check_test_expect(test, result.status == fixture->status, fixture->name);
            if (result.status == CHECK_OVERLOAD_OK || result.status == CHECK_OVERLOAD_UNAVAILABLE)
            {
                check_test_expect(test, result.candidate == fixture->selected && result.object == 100 + fixture->selected &&
                                       result.canonical == 10 + fixture->selected * 10, fixture->name);
            }
            if (result.status == CHECK_OVERLOAD_OK)
            {
                check_test_expect(test, checked.diagnostic == fixture->diagnostic && checked.value.type == returns[fixture->selected], fixture->name);
                if (fixture->arguments[0].category == CHECK_LVALUE)
                {
                    check_test_expect(test, (conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0, "overload qualification preserves original lvalue conversion");
                }
            }
            check_test_expect(test, scratch.length == 0, "overload selection and application restore caller scratch");
        }
        if (oracle)
        {
            check_test_expect(test, outcomes[i].rejected == (fixture->status != CHECK_OVERLOAD_OK || fixture->diagnostic != CHECK_DIAG_NONE), fixture->name);
        }
        unlink(paths[i]);
    }
    rmdir(directory);
    arena_release(&scratch);
}

static void check_test_overload_history(CheckTest* test, TypeTable* table)
{
    const TypeId types[] = { BUSTER_TYPE_INT, BUSTER_TYPE_DOUBLE };
    CheckOverloadCandidate candidates[258] = { 0 };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        candidates[i] = (CheckOverloadCandidate){
            .type = type_function(table, types[i], types + i, 1, BUSTER_TYPE_FLAG_PROTOTYPE),
            .declaration = 10 + i * 10,
            .canonical = 10 + i * 10,
            .object = 100 + i,
            .previous = i == 0 ? UINT32_MAX : i - 1,
            .scope = 1,
            .position = 11 + i * 10,
            .flags = BUSTER_CHECK_OVERLOADABLE,
        };
    }
    CheckValue argument = { .type = BUSTER_TYPE_DOUBLE };
    CheckOverloadRequest request = {
        .candidates = candidates,
        .count = 2,
        .head = 0,
        .arguments = &argument,
        .argument_count = 1,
        .context = { .source_position = 15 },
    };
    Arena scratch = arena_reserve(1u << 20);
    CheckOverloadResult result = check_overload_select(table, &request, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 0 && result.object == 100,
                      "captured head excludes a later better signature");
    request.head = 1;
    result = check_overload_select(table, &request, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_INVALID, "future declaration cannot be smuggled into an earlier source snapshot");
    request.context.source_position = 25;
    result = check_overload_select(table, &request, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 1, "later call uses its own expanded overload set");
    candidates[2] = candidates[1];
    candidates[2].declaration = 30;
    candidates[2].previous = 1;
    candidates[2].position = 31;
    candidates[2].flags |= BUSTER_CHECK_OVERLOAD_UNAVAILABLE;
    request.count = 3;
    request.context.source_position = 35;
    result = check_overload_select(table, &request, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 1, "later unavailable redeclaration does not mutate an earlier captured head");
    request.head = 2;
    result = check_overload_select(table, &request, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_UNAVAILABLE && result.candidate == 2 && result.canonical == 20,
                      "newest visible redeclaration replaces its canonical candidate without ambiguity");
    candidates[2].previous = 2;
    result = check_overload_select(table, &request, &scratch);
    check_test_expect(test, result.status == CHECK_OVERLOAD_INVALID, "selection rejects a cyclic candidate chain");
    CheckValue arguments[2] = {{ .type = BUSTER_TYPE_INT },{ 0 }};
    for (uint32_t i = 0; i < 257; i += 1)
    {
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 134000 + i);
        const TypeId parameters[2] = { BUSTER_TYPE_INT, type_pointer(table, record) };
        candidates[i] = (CheckOverloadCandidate){
            .type = type_function(table, BUSTER_TYPE_INT, parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE),
            .declaration = 1000 + i,
            .canonical = 1000 + i * 1024,
            .object = 1000 + i,
            .previous = i == 0 ? UINT32_MAX : i - 1,
            .scope = 2,
            .position = 1001 + i,
            .flags = BUSTER_CHECK_OVERLOADABLE,
        };
        if (i == 123)
        {
            arguments[1].type = parameters[1];
        }
    }
    request = (CheckOverloadRequest){
        .candidates = candidates,
        .count = 257,
        .head = 256,
        .arguments = arguments,
        .argument_count = 2,
        .context = { .source_position = 2000 },
    };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        request.scalar = path != 0;
        result = check_overload_select(table, &request, &scratch);
        check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 123 && result.object == 1123,
                          "large overload set retains exact candidate through canonical hash collisions");
    }
    const uint64_t capacity = scratch.capacity;
    bool succeeded = false;
    for (uint64_t limit = 0; limit < 8192; limit += 31)
    {
        scratch.capacity = limit;
        result = check_overload_select(table, &request, &scratch);
        succeeded |= result.status == CHECK_OVERLOAD_OK;
        check_test_expect(test, (result.status == CHECK_OVERLOAD_FULL || (result.status == CHECK_OVERLOAD_OK && result.candidate == 123)) && scratch.length == 0,
                          "all partial overload arena capacities fail closed and restore scratch");
    }
    check_test_expect(test, succeeded, "overload capacity sweep reaches a successful control");
    scratch.capacity = capacity;
    TypeId parameters[2][257];
    CheckValue wide_arguments[257];
    CheckConversion conversions[257];
    for (uint32_t i = 0; i < 257; i += 1)
    {
        parameters[0][i] = BUSTER_TYPE_INT;
        parameters[1][i] = BUSTER_TYPE_DOUBLE;
        wide_arguments[i] = (CheckValue){ .type = i == 128 ? BUSTER_TYPE_SHORT : BUSTER_TYPE_INT };
    }
    for (uint32_t i = 0; i < 2; i += 1)
    {
        candidates[i] = (CheckOverloadCandidate){
            .type = type_function(table, types[i], parameters[i], 257, BUSTER_TYPE_FLAG_PROTOTYPE),
            .declaration = i,
            .canonical = i,
            .object = i + 1,
            .previous = i == 0 ? UINT32_MAX : i - 1,
            .flags = BUSTER_CHECK_OVERLOADABLE,
        };
    }
    CheckAction action = {
        .op = CHECK_OP_CALL,
        .operands = {{ .type = candidates[1].type, .category = CHECK_FUNCTION }},
        .arguments = wide_arguments,
        .argument_count = 257,
        .argument_conversions = conversions,
    };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CheckActionResult checked;
        result = check_overload_call(table, &action, candidates, 2, 1, &scratch, path != 0, &checked, 0);
        check_test_expect(test, result.status == CHECK_OVERLOAD_OK && result.candidate == 0 && checked.diagnostic == CHECK_DIAG_NONE && checked.value.type == BUSTER_TYPE_INT,
                          "arbitrary argument slices cross all rank vector boundaries");
        for (uint32_t i = 0; i < 257; i += 1)
        {
            check_test_expect(test, conversions[i].type == BUSTER_TYPE_INT && (conversions[i].flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0,
                              "selected wide call retains every argument conversion");
        }
    }
    arena_release(&scratch);
}

static void check_test_overload_sources(CheckTest* test, const char* clang)
{
    const CheckSourceFixture fixtures[] = {
        { .name = "overload tgmath selection and unavailable fallback", .source = "#define O __attribute__((overloadable))\nstatic void choose(...) O __attribute__((unavailable)); static int choose(int x) O{return x;} static double choose(double x) O{return x;} _Static_assert(_Generic(choose(3),int:1,default:0),\"int\"); _Static_assert(_Generic(choose(3.0),double:1,default:0),\"double\"); int run(void){return choose(7);}\n" },
        { .name = "overload selected unavailable fallback", .source = "#define O __attribute__((overloadable))\nvoid f(...) O __attribute__((unavailable)); int f(int) O; void g(void){f((void*)1);}\n", .rejected = true },
        { .name = "overload exact source visibility", .source = "#define O __attribute__((overloadable))\nint f(int) O; _Static_assert(_Generic(f(1.0),int:1,default:0),\"before\"); double f(double) O; _Static_assert(_Generic(f(1.0),double:1,default:0),\"after\");\n" },
        { .name = "overload lexical hiding", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; void g(void){int f(int) O; _Static_assert(_Generic(f(1.0),int:1,default:0),\"inner\");} _Static_assert(_Generic(f(1.0),double:1,default:0),\"outer\");\n" },
        { .name = "overload nested calls use selected type", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; _Static_assert(_Generic(f(f(1.0)),double:1,default:0),\"nested\");\n" },
        { .name = "overload typeof uses selected type", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; __typeof__(f(1.0)) value; _Static_assert(__builtin_types_compatible_p(__typeof__(value),double),\"typeof\");\n" },
        { .name = "overload common parameter redeclaration", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; int f(int) O; _Static_assert(_Generic(f(1),int:1,default:0),\"redecl\");\n" },
        { .name = "overload changing return type rejects", .source = "#define O __attribute__((overloadable))\nint f(int) O;\ndouble f(int) O;\n", .rejected = true },
        { .name = "overload changing marked declaration rejects", .source = "#define O __attribute__((overloadable))\nint f(int) O;\nint f(int);\n", .rejected = true },
        { .name = "overload one unmarked signature", .source = "#define O __attribute__((overloadable))\nint f(int); double f(double) O; _Static_assert(_Generic(f(1),int:1,default:0),\"unmarked\"); _Static_assert(_Generic(f(1.0),double:1,default:0),\"marked\");\n" },
        { .name = "overload incompatible best matches stay ambiguous", .source = "#define O __attribute__((overloadable))\nint f(int,long,long) O; double f(long,int,int) O;\nvoid g(void){f(1,1,1);}\n", .rejected = true },
        { .name = "overload prototype required", .source = "int f() __attribute__((overloadable));\n", .rejected = true },
        { .name = "ordinary bare ellipsis remains invalid", .source = "void f(...);\n", .rejected = true },
        { .name = "overload applies only to functions", .source = "int x __attribute__((overloadable));\n", .rejected = true },
        { .name = "overload cannot annotate an enumerator", .source = "enum E { X __attribute__((overloadable)) };\n", .rejected = true },
        { .name = "overload cannot annotate a parameter", .source = "void f(int x __attribute__((overloadable)));\n", .rejected = true },
        { .name = "overload cannot annotate a record tag", .source = "struct __attribute__((overloadable)) S {int x;};\n", .rejected = true },
        { .name = "overload cannot annotate a member", .source = "struct S {int x __attribute__((overloadable));};\n", .rejected = true },
        { .name = "overload selects ordinary unmarked definition", .source = "int pick(int x){return x+5;} double pick(double) __attribute__((overloadable)); int run(void){return pick(7);}\n" },
        { .name = "overload call in raw attribute", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; typedef int T [[gnu::aligned(sizeof(f(1.0)))]]; _Static_assert(_Alignof(T)==8,\"raw call\");\n" },
        { .name = "overload zero argument call in raw attribute", .source = "#define O __attribute__((overloadable))\nlong f(void) O; int f(int) O; typedef int T [[gnu::aligned(sizeof(f()))]]; _Static_assert(_Alignof(T)==8,\"raw zero call\");\n" },
        { .name = "overload parenthesized callee", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; _Static_assert(_Generic((f)(1.0),double:1,default:0),\"grouped\");\n" },
        { .name = "overload deep pointer qualification", .source = "#define O __attribute__((overloadable))\nint f(const int*const*) O; double f(void*) O; _Static_assert(_Generic(f((int**)1),int:1,default:0),\"qualified\");\n" },
        { .name = "outer overload supplies unique function argument target", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; int take(int(*)(int)) O; double take(char*) O; _Static_assert(_Generic(take(f),int:1,default:0),\"context\");\n" },
        { .name = "outer overload preserves ambiguous function argument targets", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; int take(int(*)(int)) O; double take(double(*)(double)) O;\nvoid g(void){take(f);}\n", .rejected = true },
        { .name = "ordinary call supplies function argument target", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; int take(int(*)(int)); _Static_assert(_Generic(take(f),int:1,default:0),\"ordinary context\");\n" },
        { .name = "function pointer call supplies function argument target", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; int(*take)(int(*)(int)); _Static_assert(_Generic(take(&f),int:1,default:0),\"pointer context\");\n" },
        { .name = "raw outer call supplies function argument target", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; int take(int(*)(int)) O; double take(char*) O; typedef int T [[gnu::aligned(sizeof(take(f)))]]; _Static_assert(_Alignof(T)==4,\"raw context\");\n" },
        { .name = "raw cast selects exact function address", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; typedef int T [[gnu::aligned(sizeof((int(*)(int))f))]]; _Static_assert(_Alignof(T)==8,\"raw cast\");\n" },
        { .name = "typeof cannot choose latest overload", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O;\n__typeof__(f) p;\n", .rejected = true },
        { .name = "typeof cast supplies exact overload context", .source = "#define O __attribute__((overloadable))\nint f(int) O; double f(double) O; __typeof__((int(*)(int))f) p; _Static_assert(__builtin_types_compatible_p(__typeof__(p),int(*)(int)),\"typed address\");\n" },
    };
    char directory[] = "/tmp/buster-check-overload-source-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready &= parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* mode = "-std=gnu17";
    const bool oracle = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, oracle, "overload full source Clang oracle runs");
    for (uint32_t i = 0; oracle && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, fixtures[i].name);
        check_test_source(test, fixtures + i, paths[i], outcomes + i);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_test_overloads(CheckTest* test, const char* clang)
{
    CheckTestTypes types = { 0 };
    const bool ready = check_test_types_open(&types);
    check_test_expect(test, ready, "overload test type storage");
    if (ready)
    {
        check_test_overload_declarations(test, &types.table);
        check_test_overload_selection(test, &types.table, clang);
        check_test_overload_history(test, &types.table);
        check_test_overload_ranks(test);
    }
    check_test_types_close(&types);
}

#endif
