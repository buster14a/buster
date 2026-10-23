// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original complex arithmetic/component expectations, independently checked with Clang semantic mutations.
#ifndef BUSTER_C_CHECK_COMPLEX_TEST_INCLUDED
#define BUSTER_C_CHECK_COMPLEX_TEST_INCLUDED

static CheckValue check_test_complex_value(TypeId type, long double real, long double imaginary)
{
    const CheckValue value = {
        .type = type,
        .category = CHECK_RVALUE,
        .constant_kind = CHECK_CONSTANT_COMPLEX,
        .floating = real,
        .imaginary = imaginary,
    };
    return value;
}

static CheckValue check_test_integer_complex_value(TypeId type, uint64_t real, uint64_t imaginary)
{
    const CheckValue value = {
        .type = type,
        .category = CHECK_RVALUE,
        .constant_kind = CHECK_CONSTANT_COMPLEX,
        .integer = (unsigned __int128)real | ((unsigned __int128)imaginary << 64),
    };
    return value;
}

static void check_test_complex_literals(CheckTest* test)
{
    const char* spellings[] = { "1.0i", "2.0fi", "2.0if", "4i", "5j", "0x1.8p1Li", "6fi", "1i2", "1ii", "1ij" };
    const TypeId types[] = { BUSTER_TYPE_COMPLEX_DOUBLE, BUSTER_TYPE_COMPLEX_FLOAT, BUSTER_TYPE_COMPLEX_FLOAT,
                             BUSTER_TYPE_COMPLEX(type_index(BUSTER_TYPE_INT)), BUSTER_TYPE_COMPLEX(type_index(BUSTER_TYPE_INT)),
                             BUSTER_TYPE_COMPLEX_LONG_DOUBLE, 0, 0, 0, 0 };
    const long double imaginary[] = { 1, 2, 2, 4, 5, 3, 0, 0, 0, 0 };
    Arena scratch = arena_reserve(4096);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
    {
        const uint64_t mark = scratch.length;
        const CheckLiteral literal = check_literal_number((const uint8_t*)spellings[i], (uint32_t)strlen(spellings[i]), &scratch);
        const CheckValue value = check_literal_value(literal);
        const bool integer = i == 3 || i == 4;
        const bool correct = types[i] == 0 ? !literal.valid : literal.valid && literal.imaginary && literal.type == types[i] &&
                             value.constant_kind == CHECK_CONSTANT_COMPLEX && value.flags == 0 && value.floating == 0 &&
                             (integer ? value.integer == ((unsigned __int128)(uint64_t)imaginary[i] << 64) : value.imaginary == imaginary[i]);
        check_test_expect(test, correct && !literal.resource && scratch.length == mark,
                          "imaginary literal suffix order, rank and components match the independent Clang fixtures");
    }
    _Alignas(16) uint8_t storage[64];
    for (uint64_t capacity = 0; capacity < 8; capacity += 1)
    {
        memset(storage, 0xa5, sizeof(storage));
        Arena bounded = { .base = storage, .capacity = capacity };
        const CheckLiteral literal = check_literal_number((const uint8_t*)"1.0i", 4, &bounded);
        check_test_expect(test, !literal.valid && literal.resource && bounded.length == 0 &&
                               check_test_graph_canary(storage, capacity, sizeof(storage)),
                          "imaginary literal nested spelling allocation fails within every short scratch capacity");
    }
    arena_release(&scratch);
}

static void check_test_complex_integer_actions(CheckTest* test, TypeTable* table)
{
    const TypeId type = type_complex(table, BUSTER_TYPE_INT);
    const CheckValue left = check_test_integer_complex_value(type, 3, 4);
    const CheckValue right = check_test_integer_complex_value(type, 1, (uint32_t)-2);
    const CheckOp operations[] = { CHECK_OP_MULTIPLY, CHECK_OP_DIVIDE };
    const int32_t real[] = { 11, -1 };
    const int32_t imaginary[] = { -2, 2 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operations); i += 1)
    {
        const CheckAction action = { .op = operations[i], .operands = { left, right } };
        const CheckActionResult result = check_test_action(test, table, action);
        const CheckValue expected = check_test_integer_complex_value(type, (uint32_t)real[i], (uint32_t)imaginary[i]);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == type &&
                               result.value.constant_kind == CHECK_CONSTANT_COMPLEX && result.value.integer == expected.integer &&
                               result.value.fault == CHECK_FAULT_NONE,
                          "integer-complex multiplication and division match the configured Clang constant evaluator");
    }
    const CheckAction faulting[] = {
        { .op = CHECK_OP_ADD, .node = 97, .operands = { check_test_integer_complex_value(type, INT32_MAX, 0),
                                                      check_test_integer_complex_value(type, 1, 0) } },
        { .op = CHECK_OP_MULTIPLY, .node = 98, .operands = { check_test_integer_complex_value(type, 50000, 0),
                                                           check_test_integer_complex_value(type, 50000, 0) } },
        { .op = CHECK_OP_DIVIDE, .node = 99, .operands = { left, check_test_integer_complex_value(type, 0, 0) } },
        { .op = CHECK_OP_BIT_NOT, .node = 100, .operands = { check_test_integer_complex_value(type, 0, (uint32_t)INT32_MIN) } },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(faulting); i += 1)
    {
        const CheckActionResult result = check_test_action(test, table, faulting[i]);
        const CheckFault fault = i == 2 ? CHECK_FAULT_DIVIDE_BY_ZERO : CHECK_FAULT_SIGNED_OVERFLOW;
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_NONE &&
                               result.value.fault == fault && result.value.fault_node == faulting[i].node,
                          "strict complex component faults retain their origin without publishing a usable constant");
    }
}

static void check_test_complex_actions(CheckTest* test, TypeTable* table)
{
    const CheckValue left = check_test_complex_value(BUSTER_TYPE_COMPLEX_DOUBLE, 3, 4);
    const CheckValue right = check_test_complex_value(BUSTER_TYPE_COMPLEX_DOUBLE, 1, -2);
    const CheckOp operations[] = { CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY, CHECK_OP_DIVIDE };
    const long double real[] = { 4, 2, 11, -1 };
    const long double imaginary[] = { 2, 6, -2, 2 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operations); i += 1)
    {
        const CheckAction action = { .op = operations[i], .operands = { left, right }, .extensions = BUSTER_CHECK_EXTENSION_GNU };
        const CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_COMPLEX_DOUBLE &&
                               result.value.constant_kind == CHECK_CONSTANT_COMPLEX && result.value.floating == real[i] &&
                               result.value.imaginary == imaginary[i] && result.value.fault == CHECK_FAULT_NONE,
                          "complex binary arithmetic matches independently derived real and imaginary components");
    }
    const TypeId destinations[] = { BUSTER_TYPE_BOOL, BUSTER_TYPE_INT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_COMPLEX_FLOAT };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(destinations); i += 1)
    {
        const CheckAction action = { .op = CHECK_OP_CAST, .operands = { left }, .type = destinations[i], .extensions = BUSTER_CHECK_EXTENSION_GNU };
        const CheckActionResult result = check_test_action(test, table, action);
        const bool value = i == 0 ? result.value.integer == 1 : i == 1 ? result.value.integer == 3 :
                           i == 2 ? result.value.floating == 3 : result.value.floating == 3 && result.value.imaginary == 4;
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == destinations[i] && value,
                          "complex conversion uses both components for boolean and the real component for real arithmetic types");
    }
    const CheckAction boolean = { .op = CHECK_OP_CAST, .operands = { check_test_complex_value(BUSTER_TYPE_COMPLEX_DOUBLE, 0, 2) }, .type = BUSTER_TYPE_BOOL };
    const CheckActionResult truth = check_test_action(test, table, boolean);
    check_test_expect(test, truth.diagnostic == CHECK_DIAG_NONE && truth.value.constant_kind == CHECK_CONSTANT_INTEGER && truth.value.integer == 1,
                      "a purely imaginary value remains true after boolean conversion");
    for (uint32_t imaginary_fault = 0; imaginary_fault < 2; imaginary_fault += 1)
    {
        CheckValue input = check_test_complex_value(BUSTER_TYPE_COMPLEX_DOUBLE, imaginary_fault != 0 ? 1 : 1e100L,
                                                    imaginary_fault != 0 ? 1e100L : 1);
        input.flags = BUSTER_CHECK_SIDE_EFFECT;
        const CheckAction cast = { .op = CHECK_OP_CAST, .operands = { input }, .type = type_complex(table, BUSTER_TYPE_INT), .node = 101 };
        const CheckActionResult result = check_test_action(test, table, cast);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == cast.type &&
                               result.value.constant_kind == CHECK_CONSTANT_NONE && result.value.integer == 0 &&
                               result.value.floating == 0 && result.value.imaginary == 0 &&
                               result.value.fault == CHECK_FAULT_FLOAT_RANGE && result.value.fault_node == cast.node &&
                               (result.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0,
                          "either out-of-range complex component invalidates the whole converted constant and preserves effects");
    }
    const CheckOp rejected[] = { CHECK_OP_LESS, CHECK_OP_GREATER_EQUAL, CHECK_OP_REMAINDER, CHECK_OP_BIT_AND };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        const CheckAction action = { .op = rejected[i], .operands = { left, right }, .extensions = BUSTER_CHECK_EXTENSION_GNU };
        check_test_expect(test, check_test_action(test, table, action).diagnostic != CHECK_DIAG_NONE,
                          "complex operands do not grant ordered comparisons or integer-only operations");
    }
    const CheckAction conjugate = { .op = CHECK_OP_BIT_NOT, .operands = { left }, .extensions = BUSTER_CHECK_EXTENSION_GNU };
    const CheckActionResult conjugated = check_test_action(test, table, conjugate);
    check_test_expect(test, conjugated.diagnostic == CHECK_DIAG_NONE && conjugated.value.constant_kind == CHECK_CONSTANT_COMPLEX &&
                           conjugated.value.floating == 3 && conjugated.value.imaginary == -4,
                      "GNU tilde conjugates complex values without complementing their representation");
    for (uint32_t imaginary_part = 0; imaginary_part < 2; imaginary_part += 1)
    {
        const CheckOp op = imaginary_part != 0 ? CHECK_OP_IMAGINARY : CHECK_OP_REAL;
        const CheckAction constant = { .op = op, .operands = { left }, .extensions = BUSTER_CHECK_EXTENSION_GNU };
        const CheckActionResult result = check_test_action(test, table, constant);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_DOUBLE && result.value.category == CHECK_RVALUE &&
                               result.value.constant_kind == CHECK_CONSTANT_FLOATING && result.value.floating == (imaginary_part != 0 ? 4 : 3),
                          "complex rvalue component extraction produces the corresponding real floating value");
        const CheckAction lvalue = {
            .op = op,
            .operands = { { .type = BUSTER_TYPE_COMPLEX_DOUBLE, .category = CHECK_LVALUE, .constant_kind = CHECK_CONSTANT_ADDRESS,
                            .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 133701, .offset = 16 } },
            .extensions = BUSTER_CHECK_EXTENSION_GNU,
        };
        const CheckActionResult component = check_test_action(test, table, lvalue);
        check_test_expect(test, component.diagnostic == CHECK_DIAG_NONE && component.value.type == BUSTER_TYPE_DOUBLE && component.value.category == CHECK_LVALUE &&
                               component.value.constant_kind == CHECK_CONSTANT_ADDRESS && component.value.object == 133701 &&
                               component.value.offset == (imaginary_part != 0 ? 24 : 16),
                          "complex lvalue components retain their object and exact component byte offset");
    }
    const CheckValue scalar = { .type = BUSTER_TYPE_INT | BUSTER_TYPE_CONST, .category = CHECK_LVALUE, .object = 133702 };
    const CheckAction real_scalar = { .op = CHECK_OP_REAL, .operands = { scalar }, .extensions = BUSTER_CHECK_EXTENSION_GNU };
    const CheckActionResult identity = check_test_action(test, table, real_scalar);
    check_test_expect(test, identity.diagnostic == CHECK_DIAG_NONE && identity.value.category == CHECK_LVALUE && identity.value.type == scalar.type,
                      "scalar real extraction preserves lvalue and const qualification");
    const CheckAction imaginary_scalar = {
        .op = CHECK_OP_IMAGINARY,
        .operands = { { .type = BUSTER_TYPE_INT, .flags = BUSTER_CHECK_SIDE_EFFECT, .fault = CHECK_FAULT_DIVIDE_BY_ZERO, .fault_node = 8 } },
        .extensions = BUSTER_CHECK_EXTENSION_GNU,
    };
    const CheckActionResult evaluated = check_test_action(test, table, imaginary_scalar);
    check_test_expect(test, evaluated.diagnostic == CHECK_DIAG_NONE && evaluated.value.type == BUSTER_TYPE_INT && evaluated.value.category == CHECK_RVALUE &&
                           (evaluated.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && evaluated.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO &&
                           (evaluated.value.flags & BUSTER_CHECK_ICE) == 0,
                      "scalar imaginary extraction evaluates the original expression despite its zero mathematical result");
    const CheckAction volatile_part = {
        .op = CHECK_OP_IMAGINARY,
        .operands = { { .type = BUSTER_TYPE_COMPLEX_DOUBLE | BUSTER_TYPE_VOLATILE, .category = CHECK_LVALUE,
                        .constant_kind = CHECK_CONSTANT_ADDRESS, .flags = BUSTER_CHECK_STATIC_OBJECT, .object = 133703 } },
    };
    const CheckActionResult part = check_test_action(test, table, volatile_part);
    const CheckAction read = { .op = CHECK_OP_POSITIVE, .operands = { part.value } };
    const CheckAction address = { .op = CHECK_OP_ADDRESS, .operands = { part.value } };
    const CheckActionResult loaded = check_test_action(test, table, read);
    const CheckActionResult addressed = check_test_action(test, table, address);
    check_test_expect(test, part.diagnostic == CHECK_DIAG_NONE && part.value.type == BUSTER_TYPE_DOUBLE && part.value.category == CHECK_LVALUE &&
                           (part.value.flags & BUSTER_CHECK_VOLATILE_PART) != 0 && (part.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                           loaded.diagnostic == CHECK_DIAG_NONE && (loaded.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                           (loaded.value.flags & BUSTER_CHECK_VOLATILE_PART) == 0 && loaded.value.constant_kind == CHECK_CONSTANT_NONE &&
                           addressed.diagnostic == CHECK_DIAG_NONE && addressed.value.type == type_pointer(table, BUSTER_TYPE_DOUBLE) &&
                           addressed.value.offset == 8 && (addressed.value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_VOLATILE_PART)) == 0,
                      "complex component type stripping preserves volatile loads without adding a load to address-taking");
}

static void check_test_complex_oracles(CheckTest* test, const char* clang)
{
    const char* sources[] = {
        "_Static_assert(__real__((3.0+4.0i)*(1.0-2.0i))==11 && __imag__((3.0+4.0i)*(1.0-2.0i))==-2,\"product\");\n",
        "_Static_assert(__real__((3.0+4.0i)*(1.0-2.0i))==10,\"mutation\");\n",
        "_Static_assert(__real__((3.0+4.0i)/(1.0-2.0i))==-1 && __imag__((3.0+4.0i)/(1.0-2.0i))==2,\"quotient\");\n",
        "_Static_assert((_Bool)(0.0+2.0i)==1 && (int)(3.0+4.0i)==3,\"conversion\");\n",
        "int f(void);_Static_assert(__imag__ f()==0,\"evaluated scalar\");\n",
        "_Static_assert(__real__ 7==7 && __imag__ 7==0,\"integer components\");\n",
        "void f(void){int x;__real__ x=3;int*p=&__real__ x;}\n",
        "void f(void){int x;__imag__ x=3;}\n",
        "void f(void){int x;int*p=&__imag__ x;}\n",
        "void f(void){const int x=0;__real__ x=3;}\n",
        "void f(void){double _Complex x;__real__ x=3;__imag__ x=4;}\n",
        "void f(void){const double _Complex x=0;__imag__ x=4;}\n",
        "int f(int*p){return __imag__ p;}\n",
        "_Static_assert(__imag__(~(3.0+4.0i))==-4,\"GNU conjugation\");\n",
        "int f(double _Complex x){return x<1;}\n",
        "int f(double _Complex x){return x==1;}\n",
        "_Static_assert(__real__((3+4i)*(1-2i))==11 && __imag__((3+4i)*(1-2i))==-2,\"integer product\");\n",
        "_Static_assert(__real__((3+4i)/(1-2i))==-1 && __imag__((3+4i)/(1-2i))==2,\"integer quotient\");\n",
        "_Static_assert(__real__((3+4i)/(0+0i))==0,\"zero divisor\");\n",
        "_Static_assert(__imag__ 1.0i==1 && __imag__ 2.0fi==2 && __imag__ 2.0if==2 && __imag__ 4i==4 && __imag__ 5j==5 && __imag__ 0x1.8p1Li==3,\"imaginary literals\");\n",
        "double _Complex x=6fi;\n",
        "double _Complex x=1i2;\n",
        "double _Complex x=1ii;\n",
        "double _Complex x=1ij;\n",
    };
    const bool rejected[] = { false, true, false, false, true, false, false, true, true, true, false, false, true, false, true, false,
                              false, false, true, false, true, true, true, true };
    char directory[] = "/tmp/buster-check-complex-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(sources)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    const char* modes[] = { "-std=gnu17", "-std=gnu23", "-std=c99" };
    for (uint32_t mode = 0; ready && mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
    {
        const bool checked = parse_test_clang(clang, &modes[mode], 1, pointers, BUSTER_ARRAY_LENGTH(sources), outcomes);
        check_test_expect(test, checked, "independent complex semantic oracle runs");
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
        {
            char message[128];
            snprintf(message, sizeof(message), "independent complex mutation %u agrees in %s", i, modes[mode]);
            check_test_expect(test, outcomes[i].rejected == rejected[i], message);
            if (mode == 0)
            {
                const CheckSourceFixture fixture = { .name = message, .source = sources[i], .rejected = rejected[i] };
                check_test_source(test, &fixture, paths[i], outcomes + i);
            }
        }
    }
    check_test_expect(test, ready, "complex oracle source creation");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static __attribute__((noinline)) void check_test_complex(CheckTest* test, TypeTable* table, const char* clang)
{
    check_test_complex_literals(test);
    check_test_complex_integer_actions(test, table);
    check_test_complex_actions(test, table);
    check_test_complex_oracles(test, clang);
}

#endif
