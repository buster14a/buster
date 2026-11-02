// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original typed-varargs fixtures and independent Clang source probes (#148). Include after c_checker_test.c and
// c_check_varargs.c. The existing fieldwise value comparison and arena/type test helpers are reused. The external
// oracle deliberately runs without the parser harness's -w: non-pod-varargs is an error by default, while several
// other varargs constraints are warnings. No external implementation or test corpus is incorporated.
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>

typedef struct CheckVarargsTest CheckVarargsTest;
struct CheckVarargsTest
{
    uint64_t checks;
    uint64_t failures;
};

static void check_varargs_test_expect(CheckVarargsTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-varargs: %s\n", description);
        }
    }
}

static bool check_varargs_test_equal(const CheckVarargsResult* a, const CheckVarargsResult* b)
{
    return check_test_action_equal(&a->action, &b->action) && a->requested_type == b->requested_type &&
           a->evaluated_operands == b->evaluated_operands && a->warnings == b->warnings && a->mutates_list == b->mutates_list;
}

static TypeId check_varargs_test_list(TypeTable* table)
{
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 14801);
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_VOID);
    const TypeMember members[] = {
        {
            .name = 14802,
            .type = BUSTER_TYPE_UINT,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        },
        {
            .name = 14803,
            .type = BUSTER_TYPE_UINT,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        },
        {
            .name = 14804,
            .type = pointer,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        },
        {
            .name = 14805,
            .type = pointer,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        },
    };
    const bool complete = record != 0 && pointer != 0 && type_complete_record(table, record, members, BUSTER_ARRAY_LENGTH(members), (TypeRecordAttributes){ 0 });
    return complete ? type_array(table, record, 1) : 0;
}

static void check_varargs_test_fixtures(CheckVarargsTest* test, TypeTable* table, TypeId list)
{
    const TypeId parameter_types[] = {
        BUSTER_TYPE_INT,
    };
    const TypeId function = type_function(table, BUSTER_TYPE_INT, parameter_types, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC);
    CheckVarargsContext context = {
        .va_list_type = list,
        .function_type = function,
        .last_parameter = 17,
        .last_parameter_type = BUSTER_TYPE_INT,
        .standard = 2017,
        .variadic = true,
    };
    const CheckValue value = {
        .type = list,
        .category = CHECK_LVALUE,
        .flags = BUSTER_CHECK_STATIC_OBJECT,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = 1,
    };
    CheckVarargsAction action = {
        .id = CBUILTIN_VA_START,
        .operands = {
            value,
            {
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
                .flags = BUSTER_CHECK_SIDE_EFFECT,
                .fault = CHECK_FAULT_DIVIDE_BY_ZERO,
                .fault_node = 99,
            },
        },
        .argument_count = 2,
        .second_argument_declaration = 17,
        .node = 81,
    };
    CheckVarargsResult result;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID &&
                              result.action.value.fault == CHECK_FAULT_NONE && result.evaluated_operands == 1 && result.mutates_list &&
                              result.warnings == 0 && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && result.action.conversions[1].type == 0,
                              "va_start ignores the second operand's effects/faults and does not convert/evaluate it");
    check_varargs_test_expect(test, (result.action.conversions[0].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0 &&
                              (result.action.conversions[0].flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0,
                              "canonical array list decays into the ordinary reference-like parameter");
    action.second_argument_declaration = 18;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.warnings == BUSTER_CHECK_VARARGS_WARN_LAST_PARAMETER,
                              "wrong last-parameter identity is a warning, not an invented hard constraint");
    action.second_argument_declaration = 17;
    context.last_parameter_type = BUSTER_TYPE_FLOAT;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.warnings == BUSTER_CHECK_VARARGS_WARN_LAST_PROMOTION,
                              "promoted last parameter remains accepted with its undefined-behavior warning");
    context.last_parameter_flags = BUSTER_CHECK_REGISTER;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.warnings == BUSTER_CHECK_VARARGS_WARN_LAST_REGISTER, "register warning takes precedence over last-type promotion");
    context.last_parameter_flags = 0;
    context.last_parameter_type = BUSTER_TYPE_INT;
    context.variadic = false;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_CALL && !result.mutates_list, "va_start requires a variadic enclosing function");
    context.variadic = true;
    context.function_type = 0;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_CALL, "va_start is rejected outside a function");
    context.function_type = function;
    action.id = CBUILTIN_C23_VA_START;
    action.argument_count = 1;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED, "C23 va_start spelling is unavailable in C17");
    context.standard = 2023;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == 1 &&
                              result.warnings == BUSTER_CHECK_VARARGS_WARN_C17_COMPAT, "C23 builtin accepts one list and retains compatibility-warning metadata");
    action.argument_count = 3;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "Clang C23 builtin rejects more than two raw arguments");
    context.standard = 2023;
    action.id = CBUILTIN_VA_START;
    action.argument_count = 2;
    action.operands[1] = check_test_integer(BUSTER_TYPE_LONG, 0);
    action.second_argument_declaration = BUSTER_CHECK_VARARGS_NO_PARAMETER;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.warnings == BUSTER_CHECK_VARARGS_WARN_C17_COMPAT,
                              "C23 ordinary builtin recognizes any zero-valued integer constant expression");
    action.id = CBUILTIN_C23_VA_START;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.warnings == BUSTER_CHECK_VARARGS_WARN_LAST_PARAMETER, "zero exemption does not apply to the dedicated C23 builtin");
    action.id = CBUILTIN_VA_COPY;
    action.operands[1] = value;
    action.operands[1].fault = CHECK_FAULT_SHIFT_COUNT;
    action.operands[1].fault_node = 73;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == 3 && result.mutates_list &&
                              result.action.value.fault == CHECK_FAULT_SHIFT_COUNT && result.action.value.fault_node == 73,
                              "va_copy evaluates both list expressions and preserves their evaluation fault");
    action.id = CBUILTIN_VA_END;
    action.argument_count = 1;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.mutates_list && result.evaluated_operands == 1 &&
                              (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0, "va_end preserves lifetime/evaluation effects without inventing a store");
    const TypeId record = type_element(table, list);
    action.operands[0].type = type_incomplete_array(table, record);
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.action.conversions[0].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0,
                              "unknown outer list-array bound does not prevent legal pointer decay");
    action.id = CBUILTIN_VA_ARG;
    action.argument_count = 2;
    action.requested_type = BUSTER_TYPE_INT;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "unknown-bound list array still decays to the exact va_arg pointer type");
    action.operands[0].type = type_pointer(table, type_aligned(table, record, 5));
    action.operands[0].category = CHECK_RVALUE;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "aligned typedef on list pointee does not alter canonical C type");
    action.id = CBUILTIN_VA_END;
    action.argument_count = 1;
    action.operands[0].type = type_pointer(table, type_qualify(table, record, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE));
    action.operands[0].category = CHECK_RVALUE;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.warnings == BUSTER_CHECK_VARARGS_WARN_QUALIFIERS,
                              "ordinary list-reference prototypes retain qualifier-discard warnings");
    action.id = CBUILTIN_VA_ARG;
    action.argument_count = 2;
    action.requested_type = BUSTER_TYPE_INT;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "va_arg requires the exact decayed list type");
    action.operands[0] = value;
    const TypeId requests[] = {
        BUSTER_TYPE_INT,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_SHORT,
        type_qualify(table, BUSTER_TYPE_LONG_DOUBLE, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE),
        type_array(table, BUSTER_TYPE_INT, 3),
        type_variable_array(table, BUSTER_TYPE_INT, 17),
        BUSTER_TYPE_INT128,
        type_pointer(table, type_qualify(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST)),
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(requests); i += 1)
    {
        action.requested_type = requests[i];
        check_varargs(table, &context, &action, &result, 1);
        const uint32_t warning = i == 1 || i == 2 ? BUSTER_CHECK_VARARGS_WARN_ARGUMENT_PROMOTION : i == 4 || i == 5 ? BUSTER_CHECK_VARARGS_WARN_ARRAY_RESULT : 0;
        check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.requested_type == requests[i] &&
                                  result.action.value.type == type_unqualified(requests[i]) && result.action.value.category == CHECK_RVALUE &&
                                  result.warnings == warning && result.action.conversions[1].type == 0 && result.action.value.constant_kind == CHECK_CONSTANT_NONE,
                                  "requested type remains a typed nonconstant rvalue with actual promotion/array warnings");
    }
    const TypeId invalid[] = {
        BUSTER_TYPE_VOID,
        type_tag(table, BUSTER_TYPE_KIND_STRUCT, 14810),
        type_incomplete_array(table, BUSTER_TYPE_INT),
        type_atomic(table, BUSTER_TYPE_INT),
        type_function(table, BUSTER_TYPE_INT, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE),
        0,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        action.requested_type = invalid[i];
        check_varargs(table, &context, &action, &result, 1);
        check_varargs_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 1 && !result.mutates_list,
                                  "invalid/incomplete/default-error requested type is diagnosed at the type operand");
    }
    action.id = CBUILTIN_VA_END;
    action.argument_count = 1;
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 0);
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.action.conversions[0].flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0,
                              "ordinary list prototype accepts a null pointer constant");
    action.operands[0] = (CheckValue){ .type = type_pointer(table, BUSTER_TYPE_VOID), .category = CHECK_RVALUE };
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "ordinary list prototype accepts a void pointer conversion");
    action.id = CBUILTIN_VA_ARG;
    action.argument_count = 2;
    action.requested_type = BUSTER_TYPE_INT;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "va_arg does not use the ordinary void-pointer conversion");
    CheckVarargsAction actions[33];
    CheckVarargsResult whole[33];
    CheckVarargsResult split[33];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(actions); i += 1)
    {
        actions[i] = (CheckVarargsAction){
            .id = i % 3 == 0 ? CBUILTIN_VA_ARG : i % 3 == 1 ? CBUILTIN_VA_END : CBUILTIN_VA_COPY,
            .operands = { value, value },
            .argument_count = i % 3 == 1 ? 1 : 2,
            .requested_type = requests[i % BUSTER_ARRAY_LENGTH(requests)],
            .node = i,
        };
    }
    check_varargs(table, &context, actions, whole, BUSTER_ARRAY_LENGTH(actions));
    for (uint32_t partition = 1; partition <= 9; partition += 1)
    {
        for (uint32_t first = 0; first < BUSTER_ARRAY_LENGTH(actions);)
        {
            const uint32_t count = BUSTER_ARRAY_LENGTH(actions) - first < partition ? BUSTER_ARRAY_LENGTH(actions) - first : partition;
            check_varargs(table, &context, actions + first, split + first, count);
            first += count;
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(actions); i += 1)
        {
            check_varargs_test_expect(test, check_varargs_test_equal(whole + i, split + i), "legal varargs partitions changed types/conversions/effects/diagnostics");
        }
    }
    const TypeId late_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 14811);
    check_varargs_test_expect(test, type_enum_complete(table, late_enum, BUSTER_TYPE_INT, 0), "source-history enum fixture");
    uint32_t completions[512] = { 0 };
    const uint32_t tag = table->types.index[type_index(late_enum)];
    completions[tag] = 900;
    action = (CheckVarargsAction){
        .id = CBUILTIN_VA_ARG,
        .operands = { value },
        .argument_count = 2,
        .requested_type = late_enum,
        .context.tag_completions = completions,
        .context.tag_completion_count = BUSTER_ARRAY_LENGTH(completions),
        .context.source_position = 899,
    };
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE && result.action.diagnostic_operand == 1,
                              "later enum completion cannot authorize an earlier va_arg type name");
    action.context.source_position = 900;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "exclusive type-name end includes its completed tag");
    action.context.tag_completion_count = tag;
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "completion history capacity is checked before reading a tag position");
    action.context.tag_completion_count = BUSTER_ARRAY_LENGTH(completions);
    action.context.source_position = 899;
    action.requested_type = type_pointer(table, late_enum);
    check_varargs(table, &context, &action, &result, 1);
    check_varargs_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "pointer request does not require its pointed-to enum to be complete");
    check_varargs(NULL, NULL, NULL, NULL, 0);
}

static void check_varargs_test_pages(CheckVarargsTest* test, TypeTable* table, TypeId list)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_bytes = 17 * sizeof(CheckVarargsAction);
    const size_t output_bytes = 17 * sizeof(CheckVarargsResult);
    const size_t input_region = page != 0 ? ((input_bytes + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((output_bytes + 16 + page - 1) / page) * page : 0;
    const size_t size = input_region + output_region + 2 * page;
    uint8_t* mapping = page != 0 ? mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    if (ready)
    {
        ready = mprotect(mapping + input_region, page, PROT_NONE) == 0 &&
                mprotect(mapping + input_region + page + output_region, page, PROT_NONE) == 0;
    }
    check_varargs_test_expect(test, ready, "guarded varargs input/output allocation");
    const CheckVarargsContext context = {
        .va_list_type = list,
    };
    for (uint32_t count = 0; ready && count <= 17; count += 1)
    {
        CheckVarargsAction* actions = (CheckVarargsAction*)(mapping + input_region - count * sizeof(*actions));
        CheckVarargsResult* results = (CheckVarargsResult*)(mapping + input_region + page + output_region - count * sizeof(*results));
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        for (uint32_t i = 0; i < count; i += 1)
        {
            actions[i] = (CheckVarargsAction){
                .id = CBUILTIN_VA_END,
                .operands = {
                    {
                        .type = list,
                        .category = CHECK_LVALUE,
                    },
                },
                .argument_count = i % 2 == 0 ? 1 : UINT32_MAX,
            };
        }
        check_varargs(table, &context, actions, results, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_varargs_test_expect(test, results[i].action.diagnostic == (i % 2 == 0 ? CHECK_DIAG_NONE : CHECK_DIAG_ARGUMENT_COUNT),
                                      "guarded varargs slice changed its bounded arity result");
        }
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_varargs_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5,
                                      "varargs action wrote before its logical input/output slice");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, size);
    }
}

static bool check_varargs_test_clang_run(const char* clang, const char* path, const char* standard, const char* diagnostic, bool* rejected)
{
    char* arguments[7];
    uint32_t count = 0;
    arguments[count++] = (char*)clang;
    arguments[count++] = "-fsyntax-only";
    arguments[count++] = (char*)standard;
    arguments[count++] = "-fno-color-diagnostics";
    if (diagnostic != NULL)
    {
        arguments[count++] = (char*)diagnostic;
    }
    arguments[count++] = (char*)path;
    arguments[count] = NULL;
    posix_spawn_file_actions_t actions;
    bool ready = posix_spawn_file_actions_init(&actions) == 0;
    const bool initialized = ready;
    if (ready)
    {
        ready = posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0) == 0 &&
                posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0;
    }
    pid_t child = -1;
    if (ready)
    {
        ready = posix_spawnp(&child, clang, &actions, NULL, arguments, environ) == 0;
    }
    if (initialized)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    int status = 0;
    if (ready)
    {
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        ready = waited == child && WIFEXITED(status);
    }
    *rejected = ready && WEXITSTATUS(status) != 0;
    return ready;
}

static void check_varargs_test_oracle(CheckVarargsTest* test, const char* clang)
{
    const char* sources[] = {
        "typedef __builtin_va_list V; int f(int n,...) { V a,b; __builtin_va_start(a,n); __builtin_va_copy(b,a); int x=__builtin_va_arg(b,int); __builtin_va_end(b); __builtin_va_end(a); return x; }\n",
        "typedef __builtin_va_list V; void f(int n) { V a; __builtin_va_start(a,n); }\n",
        "typedef __builtin_va_list V; void f(int n,int m,...) { V a; __builtin_va_start(a,n); }\n",
        "typedef __builtin_va_list V; void f(register int n,...) { V a; __builtin_va_start(a,n); }\n",
        "typedef __builtin_va_list V; void f(float n,...) { V a; __builtin_va_start(a,n); }\n",
        "typedef __builtin_va_list V; void f(int n,...) { V a; __builtin_c23_va_start(a); }\n",
        "typedef __builtin_va_list V; void f(int n,...) { V a; __builtin_c23_va_start(a,(n++,n)); }\n",
        "typedef __builtin_va_list V; void f(int n,...) { V a; __builtin_c23_va_start(a,n,n); }\n",
        "typedef __builtin_va_list V; void f(int n,...) { V a; __builtin_va_start(a); }\n",
        "typedef __builtin_va_list V; void f(V a) { (void)__builtin_va_arg(a,float); }\n",
        "typedef __builtin_va_list V; void f(V a) { (void)__builtin_va_arg(a,int[2]); }\n",
        "typedef __builtin_va_list V; void f(const V a) { (void)__builtin_va_arg(a,int); }\n",
        "typedef __builtin_va_list V; void f(const V a) { __builtin_va_end(a); }\n",
        "typedef __builtin_va_list V; void f(V a) { (void)__builtin_va_arg(a,_Atomic(int)); }\n",
        "typedef __builtin_va_list V; void f(V a) { (void)__builtin_va_arg(a,int(void)); }\n",
        "typedef __builtin_va_list V; void f(V a) { (void)__builtin_va_arg(a,void); }\n",
        "typedef __builtin_va_list V; struct S; void f(V a) { (void)__builtin_va_arg(a,struct S); }\n",
        "void f(void *p) { __builtin_va_end(p); __builtin_va_end(0); }\n",
        "void f(void *p) { (void)__builtin_va_arg(p,int); }\n",
        "typedef __builtin_va_list V; void f(V a,const V b) { __builtin_va_copy(a,b); __builtin_va_copy(b,a); }\n",
        "typedef __builtin_va_list V; void f(int n,...) { V a; __builtin_va_start(a,1-1); }\n",
        "typedef __builtin_va_list V; typedef __typeof__((*(V*)0)[0]) E; extern E a[]; void f(void) { __builtin_va_end(a); (void)__builtin_va_arg(a,int); }\n",
        "typedef __builtin_va_list V; typedef __typeof__((*(V*)0)[0]) E __attribute__((aligned(32))); void f(E *p) { (void)__builtin_va_arg(p,int); }\n",
        "typedef __builtin_va_list V; typedef __typeof__((*(V*)0)[0]) E __attribute__((aligned(32))); void f(const E *p) { (void)__builtin_va_arg(p,int); }\n",
    };
    const bool rejected[] = {
        false, true, false, false, false, false, false, true, true, false, false,
        true, false, true, true, true, true, false, true, false, false, false, false, true,
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(sources) == BUSTER_ARRAY_LENGTH(rejected), "every varargs oracle source has an expected result");
    char directory[] = "/tmp/buster-check-varargs-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != NULL;
    check_varargs_test_expect(test, directory_ready, "external varargs oracle directory");
    for (uint32_t i = 0; directory_ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        char path[160];
        snprintf(path, sizeof(path), "%s/%u.c", directory, i);
        bool refused = false;
        const bool written = parse_test_write(path, sources[i], strlen(sources[i]));
        bool ready = written && check_varargs_test_clang_run(clang, path, "-std=gnu23", NULL, &refused);
        check_varargs_test_expect(test, ready && refused == rejected[i], "Clang default-severity varargs acceptance differs");
        if (i == 2 || i == 3 || i == 4 || i == 9 || i == 10)
        {
            ready = written && check_varargs_test_clang_run(clang, path, "-std=gnu23", "-Werror=varargs", &refused);
            check_varargs_test_expect(test, ready && refused, "Clang does not expose the expected varargs warning when promoted to error");
        }
        if (i == 12 || i == 19)
        {
            ready = written && check_varargs_test_clang_run(clang, path, "-std=gnu23", "-Werror=incompatible-pointer-types-discards-qualifiers", &refused);
            check_varargs_test_expect(test, ready && refused, "Clang does not expose ordinary list-pointer qualifier warnings");
        }
        if (i == 5)
        {
            ready = written && check_varargs_test_clang_run(clang, path, "-std=gnu17", NULL, &refused);
            check_varargs_test_expect(test, ready && refused, "Clang exposes the dedicated C23 va_start spelling in C17");
        }
        unlink(path);
    }
    if (directory_ready)
    {
        rmdir(directory);
    }
}

static int run_check_varargs_tests(int argc, char** argv)
{
    CheckVarargsTest test = { 0 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_varargs_test_expect(&test, options, "usage: test-check-varargs [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_varargs_test_expect(&test, initialized, "varargs type-table allocation");
        if (initialized)
        {
            const TypeId list = check_varargs_test_list(&types.table);
            check_varargs_test_expect(&test, list != 0, "canonical target-shaped va_list fixture");
            if (list != 0)
            {
                check_varargs_test_fixtures(&test, &types.table, list);
                check_varargs_test_pages(&test, &types.table, list);
            }
        }
        check_test_types_close(&types);
        check_varargs_test_oracle(&test, clang);
    }
    printf("test-check-varargs: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0;
}
