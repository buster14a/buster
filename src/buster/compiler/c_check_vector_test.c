// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent vector extension fixtures. Type results, conversion destinations and rejection boundaries are
// stated directly; both action batching and source schedulers must agree. Clang checks the original C cases.
#ifndef BUSTER_C_CHECK_VECTOR_TEST_INCLUDED
#define BUSTER_C_CHECK_VECTOR_TEST_INCLUDED

static void check_test_vector_result(CheckTest* test, TypeTable* table, CheckAction action, TypeId expected, CheckDiagnostic diagnostic, const char* description)
{
    const CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == diagnostic && (diagnostic != CHECK_DIAG_NONE || result.value.type == expected), description);
    if (diagnostic == CHECK_DIAG_NONE)
    {
        check_test_expect(test, result.value.constant_kind == CHECK_CONSTANT_NONE, "vector expression never reuses scalar constant storage as vector bits");
    }
}

static void check_test_vector_actions(CheckTest* test, TypeTable* table)
{
    const TypeId vi = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId vu = type_vector(table, BUSTER_TYPE_UINT, 4, false);
    const TypeId vf = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    const TypeId vd = type_vector(table, BUSTER_TYPE_DOUBLE, 2, false);
    const TypeId vs = type_vector(table, BUSTER_TYPE_SHORT, 8, false);
    const TypeId vl = type_vector(table, BUSTER_TYPE_LONG, 2, false);
    const TypeId vll = type_vector(table, BUSTER_TYPE_LLONG, 2, false);
    const TypeId ei = type_vector(table, BUSTER_TYPE_INT, 4, true);
    const TypeId eu = type_vector(table, BUSTER_TYPE_UINT, 4, true);
    const TypeId ef = type_vector(table, BUSTER_TYPE_FLOAT, 4, true);
    const TypeId es = type_vector(table, BUSTER_TYPE_SHORT, 4, true);
    const TypeId eb = type_vector(table, BUSTER_TYPE_BOOL, 4, true);
    const TypeId e3 = type_vector(table, BUSTER_TYPE_INT, 3, true);
    check_test_expect(test, vi != 0 && vu != 0 && vf != 0 && vd != 0 && vs != 0 && vl != 0 && vll != 0 && ei != 0 && eu != 0 && ef != 0 && es != 0 && eb != 0 && e3 != 0,
                      "vector semantic fixtures create distinct GNU and extended types");
    struct VectorBinaryFixture
    {
        CheckOp op;
        TypeId left;
        TypeId right;
        TypeId expected;
        CheckDiagnostic diagnostic;
    };
    const struct VectorBinaryFixture binary[] = {
        { CHECK_OP_ADD, vi, vf, vi, CHECK_DIAG_NONE },
        { CHECK_OP_ADD, vf, vi, vf, CHECK_DIAG_NONE },
        { CHECK_OP_MULTIPLY, vi, vs, vi, CHECK_DIAG_NONE },
        { CHECK_OP_SUBTRACT, vs, vi, vs, CHECK_DIAG_NONE },
        { CHECK_OP_DIVIDE, ei, ef, ei, CHECK_DIAG_NONE },
        { CHECK_OP_ADD, vi, ei, ei, CHECK_DIAG_NONE },
        { CHECK_OP_ADD, ei, eu, ei, CHECK_DIAG_NONE },
        { CHECK_OP_EQUAL, vu, vu, vi, CHECK_DIAG_NONE },
        { CHECK_OP_LESS, vf, vf, vi, CHECK_DIAG_NONE },
        { CHECK_OP_NOT_EQUAL, vd, vd, vll, CHECK_DIAG_NONE },
        { CHECK_OP_GREATER, vl, vl, vll, CHECK_DIAG_NONE },
        { CHECK_OP_LESS_EQUAL, ef, ef, ei, CHECK_DIAG_NONE },
        { CHECK_OP_BIT_AND, vi, vu, vi, CHECK_DIAG_NONE },
        { CHECK_OP_BIT_AND, vi, vf, 0, CHECK_DIAG_INTEGER_REQUIRED },
        { CHECK_OP_REMAINDER, vf, vf, 0, CHECK_DIAG_INTEGER_REQUIRED },
        { CHECK_OP_SHIFT_LEFT, vi, vu, vi, CHECK_DIAG_NONE },
        { CHECK_OP_SHIFT_RIGHT, vi, vs, 0, CHECK_DIAG_INCOMPATIBLE },
        { CHECK_OP_SHIFT_LEFT, ei, es, 0, CHECK_DIAG_INCOMPATIBLE },
        { CHECK_OP_SHIFT_LEFT, vf, vi, 0, CHECK_DIAG_INTEGER_REQUIRED },
        { CHECK_OP_LOGICAL_AND, vi, vi, 0, CHECK_DIAG_SCALAR_REQUIRED },
        { CHECK_OP_LOGICAL_AND, ei, ef, ei, CHECK_DIAG_NONE },
        { CHECK_OP_LOGICAL_OR, ef, ef, ei, CHECK_DIAG_NONE },
        { CHECK_OP_BIT_XOR, eb, eb, eb, CHECK_DIAG_NONE },
        { CHECK_OP_EQUAL, eb, eb, eb, CHECK_DIAG_NONE },
        { CHECK_OP_ADD, eb, eb, 0, CHECK_DIAG_ARITHMETIC_REQUIRED },
        { CHECK_OP_ADD, ei, es, 0, CHECK_DIAG_INCOMPATIBLE },
        { CHECK_OP_ADD, e3, vi, 0, CHECK_DIAG_INCOMPATIBLE },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(binary); i += 1)
    {
        const CheckAction action = {
            .op = binary[i].op,
            .operands = {
                { .type = binary[i].left },
                { .type = binary[i].right },
            },
        };
        char description[100];
        snprintf(description, sizeof(description), "vector binary fixture %u has the independent type/constraint result", i);
        check_test_vector_result(test, table, action, binary[i].expected, binary[i].diagnostic, description);
    }
    struct VectorSplatFixture
    {
        TypeId vector;
        CheckValue scalar;
        bool accepted;
    };
    const struct VectorSplatFixture splats[] = {
        { vi, { .type = BUSTER_TYPE_SHORT }, true },
        { vi, { .type = BUSTER_TYPE_UINT }, false },
        { vu, { .type = BUSTER_TYPE_INT }, true },
        { vi, { .type = BUSTER_TYPE_LONG }, false },
        { vi, check_test_integer(BUSTER_TYPE_LONG, 1), true },
        { vi, check_test_integer(BUSTER_TYPE_UINT, UINT32_C(0x80000000)), true },
        { vi, check_test_integer(BUSTER_TYPE_ULONG, UINT64_C(0x100000000)), false },
        { vf, { .type = BUSTER_TYPE_INT }, false },
        { vf, { .type = BUSTER_TYPE_SHORT }, true },
        { vd, { .type = BUSTER_TYPE_INT }, true },
        { vf, check_test_integer(BUSTER_TYPE_INT, 16777216), true },
        { vf, check_test_integer(BUSTER_TYPE_INT, 16777217), false },
        { vf, check_test_float(BUSTER_TYPE_DOUBLE, 1.0), true },
        { vf, check_test_float(BUSTER_TYPE_DOUBLE, 1.1), false },
        { vi, { .type = BUSTER_TYPE_FLOAT }, true },
        { vl, { .type = BUSTER_TYPE_FLOAT }, false },
        { vi, check_test_float(BUSTER_TYPE_DOUBLE, 1.0), false },
        { ei, { .type = BUSTER_TYPE_LONG }, true },
        { ef, { .type = BUSTER_TYPE_INT }, true },
        { ei, { .type = BUSTER_TYPE_FLOAT }, false },
        { ef, { .type = BUSTER_TYPE_COMPLEX_DOUBLE }, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(splats); i += 1)
    {
        const CheckAction action = {
            .op = CHECK_OP_ADD,
            .operands = {
                { .type = splats[i].vector },
                splats[i].scalar,
            },
        };
        char description[100];
        snprintf(description, sizeof(description), "vector splat fixture %u observes scalar rank and constant precision", i);
        check_test_vector_result(test, table, action, splats[i].vector, splats[i].accepted ? CHECK_DIAG_NONE : CHECK_DIAG_INCOMPATIBLE, description);
    }
    const TypeId sources[] = { vf, vs, ei, eu, BUSTER_TYPE_LONG, BUSTER_TYPE_FLOAT, vi, vi };
    const TypeId targets[] = { vi, vi, vi, ei, ei, ei, e3, BUSTER_TYPE_INT128 };
    const bool assignments[] = { true, true, true, false, true, true, false, false };
    const bool casts[] = { true, true, true, true, true, true, false, true };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        for (uint32_t cast = 0; cast < 2; cast += 1)
        {
            const CheckAction action = {
                .op = cast != 0 ? CHECK_OP_CAST : CHECK_OP_CONVERT,
                .operands = { { .type = sources[i] } },
                .type = targets[i],
            };
            const bool accepted = cast != 0 ? casts[i] : assignments[i];
            check_test_vector_result(test, table, action, targets[i], accepted ? CHECK_DIAG_NONE : CHECK_DIAG_INCOMPATIBLE,
                                     "vector assignment and explicit conversion keep distinct constraints");
        }
    }
    const CheckAction assignment = {
        .op = CHECK_OP_ASSIGN,
        .operands = {
            { .type = vi, .category = CHECK_LVALUE },
            { .type = vf, .flags = BUSTER_CHECK_FP_ENVIRONMENT, .fault = CHECK_FAULT_DIVIDE_BY_ZERO, .fault_node = 42 },
        },
    };
    const CheckActionResult assigned = check_test_action(test, table, assignment);
    check_test_expect(test, assigned.diagnostic == CHECK_DIAG_NONE && assigned.value.type == vi && assigned.value.category == CHECK_RVALUE &&
                           (assigned.value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT) &&
                           assigned.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && assigned.value.fault_node == 42 && assigned.conversions[1].type == vi,
                      "vector stores retain effects, inherited faults and explicit conversion destinations");
    const CheckAction update = {
        .op = CHECK_OP_ADD_ASSIGN,
        .operands = {
            { .type = vi, .category = CHECK_LVALUE },
            { .type = vf },
        },
    };
    const CheckActionResult updated = check_test_action(test, table, update);
    check_test_expect(test, updated.diagnostic == CHECK_DIAG_NONE && updated.value.type == vi && updated.store_conversion.type == vi &&
                           (updated.conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0 && (updated.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0,
                      "vector compound assignment converts one lvalue read and one stored result");
    CheckAction subscript = {
        .op = CHECK_OP_SUBSCRIPT,
        .operands = {
            { .type = vi | BUSTER_TYPE_VOLATILE, .category = CHECK_LVALUE },
            check_test_integer(BUSTER_TYPE_INT, 2),
        },
    };
    const CheckActionResult lane = check_test_action(test, table, subscript);
    check_test_expect(test, lane.diagnostic == CHECK_DIAG_NONE && lane.value.type == (BUSTER_TYPE_INT | BUSTER_TYPE_VOLATILE) && lane.value.category == CHECK_LVALUE &&
                           (lane.value.flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0 && (lane.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0,
                      "vector lane selection retains volatile lvalue without performing a vector load");
    const CheckAction read_lane = { .op = CHECK_OP_POSITIVE, .operands = { lane.value } };
    const CheckAction address_lane = { .op = CHECK_OP_ADDRESS, .operands = { lane.value } };
    const CheckActionResult loaded = check_test_action(test, table, read_lane);
    const CheckActionResult addressed = check_test_action(test, table, address_lane);
    check_test_expect(test, loaded.diagnostic == CHECK_DIAG_NONE && (loaded.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && addressed.diagnostic != CHECK_DIAG_NONE,
                      "vector lane reads are volatile and lane addresses are forbidden");
    subscript.operands[0].category = CHECK_RVALUE;
    const CheckActionResult temporary_lane = check_test_action(test, table, subscript);
    check_test_expect(test, temporary_lane.diagnostic == CHECK_DIAG_NONE && temporary_lane.value.category == CHECK_RVALUE,
                      "indexing a vector temporary does not invent a modifiable lvalue");
    const TypeId parameters[] = { vi, ei };
    const TypeId function = type_function(table, BUSTER_TYPE_INT, parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE);
    const CheckValue arguments[] = { { .type = vf }, check_test_integer(BUSTER_TYPE_LONG, 8) };
    CheckConversion conversions[2];
    const CheckAction call = {
        .op = CHECK_OP_CALL,
        .operands = { { .type = function, .category = CHECK_FUNCTION } },
        .arguments = arguments,
        .argument_count = 2,
        .argument_conversions = conversions,
    };
    const CheckActionResult called = check_test_action(test, table, call);
    check_test_expect(test, called.diagnostic == CHECK_DIAG_NONE && conversions[0].type == vi && conversions[1].type == ei,
                      "ordinary calls use the same vector assignment and scalar splat rules");
}

static void check_test_vector_members(CheckTest* test, TypeTable* table)
{
    const TypeId ei = type_vector(table, BUSTER_TYPE_INT, 4, true);
    const TypeId pair = type_vector(table, BUSTER_TYPE_INT, 2, true);
    const char* names[] = { "xy", "yx", "xx", "rgba", "s3210", "S01AF", "lo", "hi", "even", "odd", "xr", "s", "s4", "xyzzy" };
    const bool valid[] = { true, true, true, true, true, false, true, true, true, true, false, false, false, true };
    const TypeId types[] = { pair, pair, pair, ei, ei, 0, pair, pair, pair, pair, 0, 0, 0, type_vector(table, BUSTER_TYPE_INT, 5, true) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        uint8_t lanes[16];
        CheckVectorMember member;
        const bool decoded = check_vector_member_decode((const uint8_t*)names[i], (uint32_t)strlen(names[i]), lanes, sizeof(lanes), &member);
        check_test_expect(test, decoded, "bounded vector member decoder fits original fixture");
        const CheckAction action = {
            .op = CHECK_OP_MEMBER,
            .operands = { { .type = ei, .category = CHECK_LVALUE } },
            .vector_member = &member,
        };
        const CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, valid[i] ? result.diagnostic == CHECK_DIAG_NONE && result.value.type == types[i] && result.value.category == CHECK_LVALUE : result.diagnostic == CHECK_DIAG_MEMBER,
                          "decoded vector members have checked bounds and independently stated result types");
        if (valid[i])
        {
            const CheckAction store = {
                .op = CHECK_OP_ASSIGN,
                .operands = { result.value, { .type = types[i] } },
            };
            const CheckActionResult stored = check_test_action(test, table, store);
            check_test_expect(test, (stored.diagnostic == CHECK_DIAG_NONE) == (i != 2 && i != 13), "only swizzles without repeated components are writable");
            const CheckAction address = { .op = CHECK_OP_ADDRESS, .operands = { result.value } };
            const CheckActionResult addressed = check_test_action(test, table, address);
            check_test_expect(test, addressed.diagnostic != CHECK_DIAG_NONE, "vector swizzle addresses are forbidden");
        }
    }
    uint8_t guarded[3] = { 0xa5, 0xa5, 0xa5 };
    CheckVectorMember short_output;
    const bool capacity = check_vector_member_decode((const uint8_t*)"xyzw", 4, guarded + 1, 1, &short_output);
    check_test_expect(test, !capacity && short_output.kind == CHECK_VECTOR_MEMBER_INVALID && guarded[0] == 0xa5 && guarded[2] == 0xa5,
                      "vector member lane exhaustion does not publish a partial descriptor or overrun its slice");
}

static void check_test_vector_oracles(CheckTest* test, const char* clang)
{
    const char* header =
        "typedef int I __attribute__((vector_size(16)));typedef unsigned U __attribute__((vector_size(16)));"
        "typedef float F __attribute__((vector_size(16)));typedef double D __attribute__((vector_size(16)));"
        "typedef short S __attribute__((vector_size(16)));typedef long L __attribute__((vector_size(16)));"
        "typedef long long LL __attribute__((vector_size(16)));"
        "typedef int E __attribute__((ext_vector_type(4)));typedef unsigned EU __attribute__((ext_vector_type(4)));"
        "typedef float EF __attribute__((ext_vector_type(4)));typedef short ES __attribute__((ext_vector_type(4)));"
        "typedef int E3 __attribute__((ext_vector_type(3)));typedef _Bool B __attribute__((ext_vector_type(4)));\n";
    struct VectorSourceFixture
    {
        const char* source;
        bool rejected;
    };
    const struct VectorSourceFixture fixtures[] = {
        { "I f(F x){return x;}\n", false },
        { "I f(S x){return x;}\n", false },
        { "I f(int x){return x;}\n", true },
        { "E f(long x){return x;}\n", false },
        { "E f(float x){return x;}\n", false },
        { "E f(EU x){return x;}\n", true },
        { "E f(EF x){return x;}\n", true },
        { "I f(EF x){return x;}\n", false },
        { "I f(I x,F y){return x+y;}\n", false },
        { "F f(F x,I y){return x+y;}\n", false },
        { "I f(I x,S y){return x*y;}\n", false },
        { "E f(E x,EF y){return x+y;}\n", false },
        { "I f(I x,long y){return x+y;}\n", true },
        { "I f(I x){return x+1L;}\n", false },
        { "I f(I x,unsigned y){return x+y;}\n", true },
        { "U f(U x,int y){return x+y;}\n", false },
        { "F f(F x,int y){return x+y;}\n", true },
        { "F f(F x,short y){return x+y;}\n", false },
        { "D f(D x,int y){return x+y;}\n", false },
        { "F f(F x){return x+16777217;}\n", true },
        { "F f(F x){return x+16777216;}\n", false },
        { "F f(F x){return x+1.0;}\n", false },
        { "F f(F x){return x+1.1;}\n", true },
        { "I f(I x,float y){return x+y;}\n", false },
        { "L f(L x,float y){return x+y;}\n", true },
        { "E f(E x,long y){return x+y;}\n", false },
        { "E f(E x,float y){return x+y;}\n", true },
        { "I f(I x,F y){return x&y;}\n", true },
        { "F f(F x,F y){return x%y;}\n", true },
        { "I f(I x,U y){return x<<y;}\n", false },
        { "I f(I x,S y){return x<<y;}\n", true },
        { "E f(E x,ES y){return x<<y;}\n", true },
        { "I f(I x,long y){return x<<y;}\n", false },
        { "I f(long x,I y){return x<<y;}\n", false },
        { "I f(I x){return !x;}\n", true },
        { "E f(E x){return !x;}\n", false },
        { "E f(E x){return ++x;}\n", true },
        { "I f(I x,I y){return x&&y;}\n", true },
        { "E f(E x,EF y){return x&&y;}\n", false },
        { "E f(E x,E y,E z){return x?y:z;}\n", false },
        { "E f(E x){return x?1:2;}\n", false },
        { "I f(I x,I y,I z){return x?y:z;}\n", true },
        { "I f(int x,I y){return x?y:1;}\n", false },
        { "int f(I x){return x[1];}\n", false },
        { "int f(I x){return 1[x];}\n", true },
        { "int *f(I x){return &x[1];}\n", true },
        { "void f(const I x){x[1]=2;}\n", true },
        { "I f(I x,F y){x+=y;return x;}\n", false },
        { "I f(int x){return (I)x;}\n", true },
        { "I f(__int128 x){return (I)x;}\n", false },
        { "__int128 f(I x){return (__int128)x;}\n", false },
        { "long f(I x){return (long)x;}\n", true },
        { "E f(EF x){return (E)x;}\n", false },
        { "E3 f(I x){return (E3)x;}\n", true },
        { "E f(long x){return (E)x;}\n", false },
        { "int g(I,E);int f(F x,long y){return g(x,y);}\n", false },
        { "B f(B x,B y){return x&y;}\n", false },
        { "B f(B x,B y){return x+y;}\n", true },
        { "B f(B x){return x==1;}\n", false },
        { "B f(B x){return x&&1;}\n", true },
        { "E f(E x){return x.xyzw;}\n", false },
        { "E f(E x){return x.s3210;}\n", false },
        { "E f(E x){return x.rgba;}\n", false },
        { "E3 f(E x){return x.xyz;}\n", false },
        { "int f(E x){return x.s4;}\n", true },
        { "void f(E x){x.xx=x.xy;}\n", true },
        { "void f(E x){x.yx=x.xy;}\n", false },
        { "int *f(E x){return &x.x;}\n", true },
        { "int f(I x){return x.x;}\n", true },
        { "int f(B x){return x.x;}\n", true },
        { "void f(E x){x.xx.x=1;}\n", true },
        { "void f(I a,F b){_Static_assert(__builtin_types_compatible_p(__typeof__(a+b),I),\"left type\");}\n", false },
        { "void f(F a,I b){_Static_assert(__builtin_types_compatible_p(__typeof__(a+b),F),\"left type\");}\n", false },
        { "void f(I a,E b){_Static_assert(__builtin_types_compatible_p(__typeof__(a+b),E),\"extended result\");}\n", false },
        { "void f(D a,D b){_Static_assert(__builtin_types_compatible_p(__typeof__(a==b),LL),\"wide mask\");}\n", false },
        { "void f(U a,U b){_Static_assert(__builtin_types_compatible_p(__typeof__(a==b),I),\"signed mask\");}\n", false },
        { "void f(EF a,EF b){_Static_assert(__builtin_types_compatible_p(__typeof__(a==b),E),\"extended mask\");}\n", false },
        { "I object;I result=object+(I){1,2,3,4};\n", true },
        { "int f(void);I result=(I){1,2,3,4}+f();\n", true },
        { "I result=(I){1,2,3,4}/(I){0,1,1,1};\n", true },
        { "E result=(E){1,0,1,0}?(E){1,2,3,4}:(E){5,6,7,8};\n", true },
        { "unsigned __int128 result=(unsigned __int128)((I){1,2,3,4});\n", true },
        { "int result=((I){1,2,3,4}+1)[2];\n", true },
        { "int result=((E){1,2,3,4}).yx.x;\n", true },
        { "E result=((E){1,2,3,4}).yxwz;\n", true },
        { "int result=((E){1,2,3,4}).y;\n", false },
        { "int result=2*((I){1,2,3,4})[1]+1;\n", false },
        { "const I a={1};const I b=a+1;I c=b+1;\n", false },
        { "void f(void){const I a={1};static I b=a;}\n", false },
        { "void f(void){const I a={1};const I b=a+1;static I c=b+1;}\n", false },
        { "void f(void){const I a=(I){1};static I b=a;static I c=(I){1}+1;}\n", false },
        { "_Thread_local const I a={1};I b=a;\n", false },
        { "const I a={1};int b=a[0];\n", false },
        { "const E a={1};E b=(+a).yxwz;\n", false },
        { "const I a={1};I b=(1,a);\n", false },
        { "const I a;I b=a;\n", true },
        { "extern const I a;I b=a;\n", true },
        { "extern const I a;I b=a;const I a={1};\n", true },
        { "const volatile I a={1};I b=a;\n", true },
        { "void f(int x){const I a={x};static I b=a;}\n", true },
        { "void f(int x){const I a=(I){x};static I b=a;}\n", true },
        { "void f(int x){static I b=(I){x}+1;}\n", true },
        { "void f(int x){static I b=(x=1)?(I){1}:(I){2};}\n", true },
        { "void f(int x){static I b=(I){1}+(x=2);}\n", true },
        { "void f(int x){static I b=((x=1),(I){2});}\n", true },
        { "I a=(volatile I){1};I b={(volatile I){1}};\n", false },
        { "I a=+(volatile I){1};\n", true },
        { "int a=((volatile I){1})[0];\n", true },
        { "int a=((volatile E){1}).x;\n", true },
        { "const E a={1};E b=a.yxwz;\n", true },
        { "const I a={1};int x;I b=(x,a);\n", true },
        { "const I a={1};int f(void);I b=(f(),a);\n", true },
        { "const I a={1};I b=(1/0,a);\n", true },
        { "const I a={1};void f(void){I a={2};static I b=a;}\n", true },
    };
    char directory[] = "/tmp/buster-check-vector-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    char source[4096];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        const int length = snprintf(source, sizeof(source), "%s%s", header, fixtures[i].source);
        ready = ready && length > 0 && (uint32_t)length < sizeof(source) && parse_test_write(paths[i], source, (uint32_t)length);
    }
    const char* mode = "-std=gnu17";
    const bool oracle = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, oracle, "independent vector semantic Clang oracle runs");
    for (uint32_t i = 0; oracle && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char description[120];
        snprintf(description, sizeof(description), "original vector source fixture %u", i);
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, description);
        snprintf(source, sizeof(source), "%s%s", header, fixtures[i].source);
        const CheckSourceFixture fixture = {
            .name = description,
            .source = source,
            .rejected = fixtures[i].rejected,
        };
        check_test_source(test, &fixture, paths[i], outcomes + i);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static __attribute__((noinline)) void check_test_vectors(CheckTest* test, TypeTable* table, const char* clang)
{
    check_test_vector_actions(test, table);
    check_test_vector_members(test, table);
    check_test_vector_oracles(test, clang);
}

#endif
