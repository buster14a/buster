// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The constant folds of the library builtins (#148, c_builtin_library.c), and of __builtin_strlen and memcmp, whose
// calls the lowering expands (c_check_builtin_misc.c). Include after c_check_actions.c, c_check_builtin_math.c and
// c_builtin_library.c. clang folds a call of a constexpr builtin (Builtins.td's E attribute) wherever its constant
// evaluator can (ExprConstant.cpp), and its -O0 code does too: EmitBuiltinExpr emits the integer or floating constant
// of a call that evaluates without side effects instead of the call. So a fold is the call's value in every context:
// in an integer constant expression (an integer fold, which clang's ICE check takes as one: CheckEvalInICE), a static
// initializer, and code, where it stands for the call. The folds:
//
// - the comparisons of narrow strings (strcmp, strncmp, memcmp and bcmp, element by element as unsigned char) and of
//   wide ones (wcscmp, wcsncmp and wmemcmp, as wchar_t, a signed int): -1, 0 or 1, the sign of the first difference,
//   where the C library's functions return the difference itself (glibc's strcmp("a", "c") is -2, clang's fold -1);
// - strlen and wcslen;
// - abs, labs and llabs, but of the type's least value, whose negation overflows (clang leaves it to the code);
// - fmax, fmin, fmaximum_num and fminimum_num of float, double and long double, IEEE 754's maximumNumber and
//   minimumNumber, with -0 less than +0 and a quiet NaN beside a number the number, and copysignl.
//
// (c_builtin_library.c's cbuiltin_library_fold classifies them.) A call of the unit's own declaration of one of the plain
// library builtins clang folds (strlen, strcmp, memcmp and their kin: cbuiltin_library_plain_folds) folds the same where
// clang makes the declaration the builtin, which the reader marks in the call's member (check_input_library_call,
// BUSTER_CHECK_LIBRARY_CALL): strcmp("a", "c") is -1 there too.
//
// A fold reads its strings' elements from the unit's constant objects (CheckConstValues, check_const_load): string
// literals, at any offset an address constant has ("abc" + 1, &"abc"[1]), and the const arrays whose initializers the
// reader kept, which clang reads too (static const char s[] = "abc"; strlen(s) is 3). A call with an argument that has
// a side effect, a fault or a floating environment's dependence isn't folded, as clang's evaluation with side effects
// isn't used, and neither is one whose strings run past BUSTER_CHECK_LIBRARY_ELEMENTS elements unread.
// Not folded, as calls: the builtins whose results are pointers (memchr, strchr, wcschr, wmemchr; clang's code calls
// them too), wmemcpy and wmemmove (C++'s constant evaluation alone), _Float128's (whose constants the checker carries
// in long double), and the maximum and minimum of two NaNs or a signaling one (whose quieting and payload the call
// decides alike).
#ifndef BUSTER_C_CHECK_BUILTIN_LIBRARY_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_LIBRARY_INCLUDED

// The most elements a fold reads of each string before it gives up and leaves the call to the code.
#define BUSTER_CHECK_LIBRARY_ELEMENTS 65536u

// Element `index` of the array that the address constant `address` points into, from the unit's constant objects
// (check_const_load, at the call's source position): a narrow string's as unsigned char, a wide one's as wchar_t's int.
// The object's own elements may be of either signedness (a literal's bytes take any, a brace-initialized array's values
// only their own type), so each is tried. False where the address isn't a constant object's or the element is past it.
static bool check_builtin_library_element(const TypeTable* types, const CheckValue* address, uint64_t index, bool wide, const CheckConstValues* values,
                                          uint32_t position, int64_t* element)
{
    static const TypeId narrow[3] = { BUSTER_TYPE_CHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SCHAR };
    static const TypeId wide_types[2] = { BUSTER_TYPE_INT, BUSTER_TYPE_UINT };
    const uint32_t size = wide ? 4 : 1;
    const TypeId* candidates = wide ? wide_types : narrow;
    const uint32_t candidate_count = wide ? 2 : 3;
    const bool known = values != NULL && address->constant_kind == CHECK_CONSTANT_ADDRESS && address->object != 0 && address->fault == CHECK_FAULT_NONE &&
                       address->offset >= 0 && address->offset <= INT64_MAX - (int64_t)BUSTER_CHECK_LIBRARY_ELEMENTS * 4 && index <= BUSTER_CHECK_LIBRARY_ELEMENTS;
    bool result = false;
    CheckValue loaded = { 0 };
    for (uint32_t c = 0; known && !result && c < candidate_count; c += 1)
    {
        result = check_const_load(types, values, address->object, address->offset + (int64_t)(index * size), 0, 0, candidates[c], position, &loaded) &&
                 loaded.constant_kind == CHECK_CONSTANT_INTEGER;
    }
    // A narrow element compares as unsigned char; a wide one as a signed int, sign-extended from its 32 bits.
    *element = !result ? 0 : wide ? (int64_t)(int32_t)(uint32_t)loaded.integer : (int64_t)(uint8_t)loaded.integer;
    return result;
}

// The comparison of the strings or arrays at `a` and `b` (CBUILTIN_LIBRARY_FOLD_COMPARE*), of at most `limit` elements:
// -1, 0 or 1 into *result. False where an element it reads isn't known.
static bool check_builtin_library_compare(const TypeTable* types, const CheckValue* a, const CheckValue* b, uint64_t limit, bool wide, bool memory,
                                          const CheckConstValues* values, uint32_t position, int32_t* result)
{
    bool decided = limit == 0;
    bool known = true;
    int32_t order = 0;
    for (uint64_t i = 0; known && !decided && i < BUSTER_CHECK_LIBRARY_ELEMENTS; i += 1)
    {
        int64_t x = 0;
        int64_t y = 0;
        known = check_builtin_library_element(types, a, i, wide, values, position, &x) && check_builtin_library_element(types, b, i, wide, values, position, &y);
        if (known)
        {
            order = x < y ? -1 : x > y ? 1 : 0;
            decided = order != 0 || (!memory && x == 0) || i + 1 == limit;
        }
    }
    *result = order;
    return known && decided;
}

// The length of the string at `a`, its elements before its first NUL, into *result. False where an element it reads
// isn't known.
static bool check_builtin_library_length(const TypeTable* types, const CheckValue* a, bool wide, const CheckConstValues* values, uint32_t position,
                                         uint64_t* result)
{
    bool decided = false;
    bool known = true;
    uint64_t length = 0;
    for (uint64_t i = 0; known && !decided && i < BUSTER_CHECK_LIBRARY_ELEMENTS; i += 1)
    {
        int64_t x = 0;
        known = check_builtin_library_element(types, a, i, wide, values, position, &x);
        decided = known && x == 0;
        length = i;
    }
    *result = length;
    return known && decided;
}

// Whether a NaN is quiet: its significand's bit 62 (x87's), read from its bytes rather than by the host's arithmetic.
static bool check_builtin_library_quiet(long double value)
{
    uint8_t bytes[16];
    memcpy(bytes, &value, sizeof(bytes));
    return (bytes[7] & 0x40) != 0;
}

// The maximum (or with `minimum` the minimum) of two floating values, IEEE 754's maximumNumber and minimumNumber, which
// C's fmax and fmin and their _num kin compute: of a quiet NaN and a number, the number; of two zeros of opposite signs,
// +0 for the maximum and -0 for the minimum, which the sign bits decide. Neither may be a signaling NaN, and only one a
// quiet one (the caller's test).
static long double check_builtin_library_extreme(long double x, long double y, bool minimum)
{
    const bool zeros = x == 0 && y == 0 && __builtin_signbit(x) != __builtin_signbit(y);
    const long double larger = zeros ? (__builtin_signbit(x) ? y : x) : x >= y ? x : y;
    const long double smaller = zeros ? (__builtin_signbit(x) ? x : y) : x <= y ? x : y;
    const long double extreme = minimum ? smaller : larger;
    return x != x ? y : y != y ? x : extreme;
}

// A call's checked value, `result`, folded by `fold` where clang folds it (the header's list), from its arguments
// converted as its check converted them (`conversions`): an integer fold is an integer constant expression, a floating
// one a constant of the call's type. Any other call keeps its value.
static void check_builtin_library_fold_call(TypeTable* types, CBuiltinLibraryFold fold, bool wide, const CheckAction* action, const CheckConversion* conversions,
                                            CheckActionResult* result)
{
    const uint32_t count = action->argument_count;
    CheckValue converted[3] = { 0 };
    bool clean = fold != CBUILTIN_LIBRARY_FOLD_NONE && count <= 3 && (count == 0 || (action->arguments != NULL && conversions != NULL)) &&
                 result->diagnostic == CHECK_DIAG_NONE;
    for (uint32_t i = 0; clean && i < count; i += 1)
    {
        CheckAction operand = *action;
        operand.operands[0] = action->arguments[i];
        CheckActionResult temporary = { 0 };
        converted[i] = check_action_operand(types, &operand, &temporary, 0);
        check_action_conversion(types, converted + i, conversions[i].type, 0, temporary.conversions, action->node);
        clean = temporary.diagnostic == CHECK_DIAG_NONE && converted[i].fault == CHECK_FAULT_NONE &&
                (converted[i].flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    }
    const CheckConstValues* values = action->const_values;
    const uint32_t position = action->source_position;
    CheckValue folded = { .type = result->value.type, .category = CHECK_RVALUE };
    bool done = false;
    if (clean && (fold == CBUILTIN_LIBRARY_FOLD_COMPARE || fold == CBUILTIN_LIBRARY_FOLD_COMPARE_BOUNDED || fold == CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY))
    {
        const bool bounded = fold != CBUILTIN_LIBRARY_FOLD_COMPARE;
        const bool limited = !bounded || (count == 3 && converted[2].constant_kind == CHECK_CONSTANT_INTEGER);
        const uint64_t limit = !bounded ? UINT64_MAX : limited ? (uint64_t)converted[2].integer : 0;
        int32_t order = 0;
        done = count >= 2 && limited &&
               check_builtin_library_compare(types, converted, converted + 1, limit, wide, fold == CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY, values, position, &order);
        folded.constant_kind = CHECK_CONSTANT_INTEGER;
        folded.integer = (unsigned __int128)(__int128)order & check_action_mask(32);
    }
    else if (clean && fold == CBUILTIN_LIBRARY_FOLD_LENGTH)
    {
        uint64_t length = 0;
        done = count == 1 && check_builtin_library_length(types, converted, wide, values, position, &length);
        folded.constant_kind = CHECK_CONSTANT_INTEGER;
        folded.integer = length;
    }
    else if (clean && fold == CBUILTIN_LIBRARY_FOLD_ABSOLUTE)
    {
        uint32_t width = 0;
        bool is_signed = false;
        const bool integer = count == 1 && converted[0].constant_kind == CHECK_CONSTANT_INTEGER &&
                             check_action_integer_type(types, result->value.type, &width, &is_signed) && is_signed && width >= 2 && width <= 64;
        const uint64_t mask = integer ? (uint64_t)check_action_mask(width) : 0;
        const uint64_t bits = (uint64_t)converted[0].integer & mask;
        const bool negative = integer && ((bits >> (width - 1)) & 1) != 0;
        const uint64_t least = integer ? UINT64_C(1) << (width - 1) : 0;
        done = integer && bits != least;
        folded.constant_kind = CHECK_CONSTANT_INTEGER;
        folded.integer = negative ? (0 - bits) & mask : bits;
    }
    else if (clean && (fold == CBUILTIN_LIBRARY_FOLD_MAXIMUM || fold == CBUILTIN_LIBRARY_FOLD_MINIMUM))
    {
        const long double x = converted[0].floating;
        const long double y = converted[1].floating;
        const bool numbers = (x == x || (y == y && check_builtin_library_quiet(x))) && (y == y || (x == x && check_builtin_library_quiet(y)));
        done = count == 2 && converted[0].constant_kind == CHECK_CONSTANT_FLOATING && converted[1].constant_kind == CHECK_CONSTANT_FLOATING && numbers;
        folded.constant_kind = CHECK_CONSTANT_FLOATING;
        folded.floating = done ? check_builtin_library_extreme(x, y, fold == CBUILTIN_LIBRARY_FOLD_MINIMUM) : 0;
    }
    else if (clean && fold == CBUILTIN_LIBRARY_FOLD_COPY_SIGN)
    {
        done = count == 2 && converted[0].constant_kind == CHECK_CONSTANT_FLOATING && converted[1].constant_kind == CHECK_CONSTANT_FLOATING;
        if (done)
        {
            check_builtin_math_copy_sign(converted, converted + 1, &folded);
        }
    }
    if (done)
    {
        // An integer fold is an integer constant expression, whatever its operands were (a string's address isn't
        // one), as clang's ICE check takes a constexpr builtin's evaluated call; a floating one never is.
        folded.flags = folded.constant_kind == CHECK_CONSTANT_INTEGER ? BUSTER_CHECK_ICE : 0;
        result->value = folded;
    }
}

// A library builtin's call (c_builtin_library.c), from the conversions its ordinary check wrote.
static void check_builtin_library_fold(TypeTable* types, CBuiltinLibraryId id, const CheckAction* action, CheckActionResult* result)
{
    bool wide = false;
    const CBuiltinLibraryFold fold = cbuiltin_library_fold(id, &wide);
    check_builtin_library_fold_call(types, fold, wide, action, action->argument_conversions, result);
}

// __builtin_strlen's and __builtin_memcmp's calls (c_check_builtin_misc.c), which clang folds as it folds strlen and
// memcmp, from the conversions their check made.
static void check_builtin_library_fold_memory(TypeTable* types, CBuiltinId id, const CheckAction* action, const CheckConversion* conversions,
                                              CheckActionResult* result)
{
    const CBuiltinLibraryFold fold = id == CBUILTIN_STRLEN ? CBUILTIN_LIBRARY_FOLD_LENGTH : id == CBUILTIN_MEMCMP ? CBUILTIN_LIBRARY_FOLD_COMPARE_MEMORY : CBUILTIN_LIBRARY_FOLD_NONE;
    check_builtin_library_fold_call(types, fold, false, action, conversions, result);
}

// Whether one type of a prototype's encoding, from *position, is `type`: int ('i', and wchar_t's 'w'), size_t's unsigned
// long ('z'), char ('c') or void ('v'), each const after a 'C' and a pointer to that after a '*'. The table's
// queries alone read it; it interns nothing, so that a lane's reader asks it as source order's does.
static bool check_library_plain_type(const TypeTable* types, TypeId type, const char* prototype, uint32_t* position)
{
    const char code = prototype[*position];
    const TypeId base = code == 'i' || code == 'w' ? BUSTER_TYPE_INT : code == 'z' ? BUSTER_TYPE_ULONG : code == 'c' ? BUSTER_TYPE_CHAR : BUSTER_TYPE_VOID;
    *position += 1;
    const bool constant = prototype[*position] == 'C';
    *position += constant;
    const bool pointer = prototype[*position] == '*';
    *position += pointer;
    const TypeId plain = type_strip_aligned(types, type);
    const TypeId element = pointer && type_kind(types, plain) == BUSTER_TYPE_KIND_POINTER ? type_element(types, plain) : plain;
    const bool result = (!pointer || type_kind(types, plain) == BUSTER_TYPE_KIND_POINTER) && type_unqualified(element) == base &&
                        type_qualifiers(element) == (constant ? BUSTER_TYPE_CONST : 0) && (pointer || type_qualifiers(type) == 0);
    return result;
}

// Whether a declaration's type is a plain library builtin's prototype, as clang requires for the declaration to be the
// builtin (hasSameFunctionTypeIgnoringExceptionSpec): its result and each parameter, and a prototype without "...".
static bool check_library_plain_prototype(const TypeTable* types, TypeId declared, uint32_t index)
{
    const char* prototype = cbuiltin_library_plain_folds[index].prototype;
    const TypeId function = type_strip_aligned(types, declared);
    bool result = type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION;
    const uint8_t flags = result ? types->types.flags[type_index(function)] : 0;
    result = result && (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 && (flags & BUSTER_TYPE_FLAG_VARIADIC) == 0;
    uint32_t position = 0;
    result = result && check_library_plain_type(types, type_return(types, function), prototype, &position);
    uint32_t count = 0;
    const TypeId* parameters = result ? type_parameters(types, function, &count) : NULL;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        result = prototype[position] != 0 && check_library_plain_type(types, parameters[i], prototype, &position);
    }
    return result && prototype[position] == 0;
}

// A CALL node's member (CheckGraph.members) with this bit set and the low bits an index of cbuiltin_library_plain_folds
// marks a call of the unit's declaration of a plain library builtin that folds (c_builtin_library.c, the reader's
// check_input_library_call). A K&R call's member, its designator's token plus one, never is: its declaration has no prototype.
#define BUSTER_CHECK_LIBRARY_CALL 0x80000000u

// A marked call's value (BUSTER_CHECK_LIBRARY_CALL in `member`), folded by its function's fold, from the conversions its
// check wrote; any other call keeps its value.
static void check_builtin_library_fold_plain(TypeTable* types, uint32_t member, const CheckAction* action, CheckActionResult* result)
{
    const uint32_t index = member & ~BUSTER_CHECK_LIBRARY_CALL;
    if ((member & BUSTER_CHECK_LIBRARY_CALL) != 0 && index < BUSTER_CBUILTIN_LIBRARY_PLAIN_FOLDS)
    {
        const CBuiltinLibraryPlainFold* plain = cbuiltin_library_plain_folds + index;
        check_builtin_library_fold_call(types, plain->fold, plain->wide, action, action->argument_conversions, result);
    }
}

#endif
