// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The error clang gives for a declaration's specifiers that don't combine (#1127), which check_input_specifiers refuses
// by its own rules: its message and its place, from the specifiers' tokens as clang's DeclSpec reads them. A specifier
// that repeats one of its kind (`short short`, `static static`, `const const`) is a warning only; one that conflicts
// with an earlier one of its kind (a second type, `long long long`, `signed unsigned`, `static extern`) is an error at
// it, naming the earlier; then, once the list ends, a sign or a width the type takes neither of (`unsigned double`,
// `long char`) is an error at the sign or the first width, and so is `_Complex` with a type other than a floating one,
// and a thread storage class with another storage class than static or extern. Only refusals call this, so it costs
// valid code nothing: c_check_input.c includes it before check_input_specifiers.

// What clang calls each specifier in its messages (DeclSpec::getSpecifierName), 0 for none.
#define BUSTER_CHECK_DECLSPEC_NAMES(X) \
    X(NONE, "") \
    X(VOID, "void") \
    X(CHAR, "char") \
    X(INT, "int") \
    X(INT128, "__int128") \
    X(BIT_INT, "_BitInt") \
    X(HALF, "half") \
    X(FLOAT, "float") \
    X(DOUBLE, "double") \
    X(FLOAT16, "_Float16") \
    X(FLOAT128, "__float128") \
    X(BF16, "__bf16") \
    X(BOOL_C99, "_Bool") \
    X(BOOL, "bool") \
    X(ENUM, "enum") \
    X(UNION, "union") \
    X(STRUCT, "struct") \
    X(TYPE_NAME, "type-name") \
    X(TYPEOF, "typeof") \
    X(TYPEOF_UNQUAL, "typeof_unqual") \
    X(AUTO_TYPE, "__auto_type") \
    X(ATOMIC, "_Atomic") \
    X(SHORT, "short") \
    X(LONG, "long") \
    X(LONG_LONG, "long long") \
    X(SIGNED, "signed") \
    X(UNSIGNED, "unsigned") \
    X(COMPLEX, "complex") \
    X(TYPEDEF, "typedef") \
    X(EXTERN, "extern") \
    X(PRIVATE_EXTERN, "__private_extern__") \
    X(STATIC, "static") \
    X(AUTO, "auto") \
    X(REGISTER, "register") \
    X(GNU_THREAD, "__thread") \
    X(THREAD_LOCAL, "thread_local") \
    X(THREAD_LOCAL_C11, "_Thread_local")

typedef enum CheckDeclspecName
{
#define BUSTER_CHECK_DECLSPEC_NAME_ENUM(name, text) CHECK_DECLSPEC_##name,
    BUSTER_CHECK_DECLSPEC_NAMES(BUSTER_CHECK_DECLSPEC_NAME_ENUM)
#undef BUSTER_CHECK_DECLSPEC_NAME_ENUM
    CHECK_DECLSPEC_NAME_COUNT,
} CheckDeclspecName;

static const char* const check_declspec_names[CHECK_DECLSPEC_NAME_COUNT] =
{
#define BUSTER_CHECK_DECLSPEC_NAME_TEXT(name, text) text,
    BUSTER_CHECK_DECLSPEC_NAMES(BUSTER_CHECK_DECLSPEC_NAME_TEXT)
#undef BUSTER_CHECK_DECLSPEC_NAME_TEXT
};

// The error's kind: its message.
typedef enum CheckDeclspecError
{
    CHECK_DECLSPEC_ERROR_NONE,
    // "cannot combine with previous '%s' declaration specifier": `first` names the earlier specifier.
    CHECK_DECLSPEC_ERROR_COMBINE,
    // "'%s' cannot be signed or unsigned": `first` names the type.
    CHECK_DECLSPEC_ERROR_SIGN,
    // "'%s %s' is invalid": `first` names the width, `second` the type.
    CHECK_DECLSPEC_ERROR_WIDTH,
    // "'_Complex %s' is invalid": `first` names the type.
    CHECK_DECLSPEC_ERROR_COMPLEX,
} CheckDeclspecError;

typedef struct CheckDeclspecResult CheckDeclspecResult;
struct CheckDeclspecResult
{
    uint32_t token;
    uint8_t error;
    uint8_t first;
    uint8_t second;
};

// The slots a list read without an error holds once it ends (check_declspec_scan, #1447): its type, width and sign
// (CHECK_DECLSPEC_*), whether it has _Complex, its type's token, and whether every specifier was one the scan knows.
typedef struct CheckDeclspecSlots CheckDeclspecSlots;
struct CheckDeclspecSlots
{
    uint32_t type_token;
    uint8_t type;
    uint8_t width;
    uint8_t sign;
    bool complex;
    bool known;
};

// The token after a group that opens at `open` (a `(`, `[` or `{`), balanced over all three, or `end`.
static uint32_t check_declspec_skip_group(const Parser* parser, uint32_t open, uint32_t end)
{
    uint32_t depth = 0;
    uint32_t i = open;
    do
    {
        const uint8_t kind = parser->tokens.kinds[i];
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        depth -= depth != 0 && (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE);
        i += 1;
    } while (i < end && depth != 0);
    return i;
}

// The token after a struct, union or enum specifier at `keyword`: its attributes, its tag, an enum's base and its body.
static uint32_t check_declspec_skip_tag(const Parser* parser, uint32_t keyword, uint32_t end)
{
    uint32_t i = keyword + 1;
    bool reading = true;
    while (reading && i < end)
    {
        const uint8_t kind = parser->tokens.kinds[i];
        if (kind == PARSE_KIND_ATTRIBUTE && i + 1 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_L_PAREN)
        {
            i = check_declspec_skip_group(parser, i + 1, end);
        }
        else if (kind == PARSE_KIND_L_SQUARE && i + 1 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_L_SQUARE)
        {
            i = check_declspec_skip_group(parser, i, end);
        }
        else if (kind == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] != PARSE_ROLE_SPECIFIER)
        {
            i += 1;
        }
        else if (kind == PARSE_KIND_COLON && parser->tokens.kinds[keyword] == PARSE_KIND_ENUM)
        {
            // An enum's base: its specifiers, to its body or what ends the declaration.
            i += 1;
            while (i < end && parser->tokens.kinds[i] != PARSE_KIND_L_BRACE && parser->tokens.kinds[i] != PARSE_KIND_SEMI)
            {
                i = parser->tokens.kinds[i] == PARSE_KIND_L_PAREN ? check_declspec_skip_group(parser, i, end) : i + 1;
            }
        }
        else
        {
            i = kind == PARSE_KIND_L_BRACE ? check_declspec_skip_group(parser, i, end) : i;
            reading = false;
        }
    }
    return i;
}

// A type keyword's name, 0 for one that is not a type specifier's (or that this doesn't know).
static uint8_t check_declspec_type_name(uint32_t name, bool c23)
{
    uint8_t result = CHECK_DECLSPEC_NONE;
    switch (name)
    {
        case C_NAME_VOID: result = CHECK_DECLSPEC_VOID; break;
        case C_NAME_CHAR: result = CHECK_DECLSPEC_CHAR; break;
        case C_NAME_INT: result = CHECK_DECLSPEC_INT; break;
        case C_NAME_INT128: result = CHECK_DECLSPEC_INT128; break;
        case C_NAME_FP16: result = CHECK_DECLSPEC_HALF; break;
        case C_NAME_FLOAT: result = CHECK_DECLSPEC_FLOAT; break;
        case C_NAME_DOUBLE: result = CHECK_DECLSPEC_DOUBLE; break;
        case C_NAME_FLOAT16: result = CHECK_DECLSPEC_FLOAT16; break;
        case C_NAME_FLOAT128: result = CHECK_DECLSPEC_FLOAT128; break;
        case C_NAME_BF16: result = CHECK_DECLSPEC_BF16; break;
        // C23's `bool` is a keyword, and clang names `_Bool` after it then.
        case C_NAME_BOOL: case C_NAME_BOOL_C99: result = c23 ? CHECK_DECLSPEC_BOOL : CHECK_DECLSPEC_BOOL_C99; break;
        case C_NAME_AUTO_TYPE: result = CHECK_DECLSPEC_AUTO_TYPE; break;
        default: break;
    }
    return result;
}

// clang's first error for the specifiers from `first` to `end` (a declaration's, a parameter's or a type name's), into
// `result`. False if clang gives none, or if a specifier is one this doesn't model, which the caller's own report keeps.
// check_declspec_error's scan, with the token `skip` left out (BUSTER_CHECK_NONE for none), which a specifier clang drops
// is (#1447, check_input_specifiers_recover), and the slots it ends with in `slots`.
static __attribute__((noinline, cold)) bool check_declspec_scan(const Parser* parser, uint32_t first, uint32_t end, uint32_t skip, CheckDeclspecResult* result,
                                                                CheckDeclspecSlots* slots)
{
    const bool c23 = parser->language.standard >= 2023;
    // The type, the width, the sign, _Complex, the storage class, the thread storage class and constexpr: each one's
    // name and token, as clang's DeclSpec holds them.
    uint8_t type = CHECK_DECLSPEC_NONE;
    uint8_t width = CHECK_DECLSPEC_NONE;
    uint8_t sign = CHECK_DECLSPEC_NONE;
    uint8_t storage = CHECK_DECLSPEC_NONE;
    uint8_t thread = CHECK_DECLSPEC_NONE;
    uint32_t type_token = BUSTER_CHECK_NONE;
    uint32_t width_token = BUSTER_CHECK_NONE;
    uint32_t sign_token = BUSTER_CHECK_NONE;
    uint32_t complex_token = BUSTER_CHECK_NONE;
    uint32_t storage_token = BUSTER_CHECK_NONE;
    uint32_t thread_token = BUSTER_CHECK_NONE;
    uint32_t constexpr_token = BUSTER_CHECK_NONE;
    *result = (CheckDeclspecResult){ .token = BUSTER_CHECK_NONE };
    uint32_t i = first;
    bool reading = true;
    bool known = true;
    while (reading && known && result->error == CHECK_DECLSPEC_ERROR_NONE && i < end)
    {
        const uint32_t name = parser->tokens.names[i];
        const uint8_t kind = parser->tokens.kinds[i];
        const bool group = i + 1 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_L_PAREN;
        // The specifier's kind's slot: a type (one), a width, a sign, a storage class or a thread storage class.
        uint8_t new_type = CHECK_DECLSPEC_NONE;
        uint8_t new_width = CHECK_DECLSPEC_NONE;
        uint8_t new_sign = CHECK_DECLSPEC_NONE;
        uint8_t new_storage = CHECK_DECLSPEC_NONE;
        uint8_t new_thread = CHECK_DECLSPEC_NONE;
        uint32_t next = i + 1;
        if (i == skip)
        {
            // (A keyword clang drops: none of the slots.)
        }
        else if ((kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ALIGNAS) && group)
        {
            next = check_declspec_skip_group(parser, i + 1, end);
        }
        else if (kind == PARSE_KIND_L_SQUARE && i + 1 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_L_SQUARE)
        {
            next = check_declspec_skip_group(parser, i, end);
        }
        else if (kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_EXTENSION || kind == PARSE_KIND_CALLING_CONVENTION || (kind == PARSE_KIND_ATOMIC && !group))
        {
            // A qualifier (repeated, a warning), __extension__ or a calling convention: none of the slots.
        }
        else if (kind == PARSE_KIND_ATOMIC)
        {
            new_type = CHECK_DECLSPEC_ATOMIC;
            next = check_declspec_skip_group(parser, i + 1, end);
        }
        else if ((name == C_NAME_BITINT || name == C_NAME_EXTINT) && group)
        {
            // Before typeof's: the parser's kind of _BitInt is typeof's too, and clang names it '_BitInt' (review on #1820).
            new_type = CHECK_DECLSPEC_BIT_INT;
            next = check_declspec_skip_group(parser, i + 1, end);
        }
        else if (kind == PARSE_KIND_TYPEOF && group)
        {
            const bool unqualified = name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2;
            new_type = unqualified ? CHECK_DECLSPEC_TYPEOF_UNQUAL : CHECK_DECLSPEC_TYPEOF;
            next = check_declspec_skip_group(parser, i + 1, end);
        }
        else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
        {
            new_type = kind == PARSE_KIND_ENUM ? CHECK_DECLSPEC_ENUM : name == C_NAME_UNION ? CHECK_DECLSPEC_UNION : CHECK_DECLSPEC_STRUCT;
            next = check_declspec_skip_tag(parser, i, end);
        }
        else if (kind == PARSE_KIND_TYPEDEF)
        {
            new_storage = CHECK_DECLSPEC_TYPEDEF;
        }
        else if (kind == PARSE_KIND_STORAGE)
        {
            switch (name)
            {
                case C_NAME_EXTERN: new_storage = CHECK_DECLSPEC_EXTERN; break;
                case C_NAME_PRIVATE_EXTERN: new_storage = CHECK_DECLSPEC_PRIVATE_EXTERN; break;
                case C_NAME_STATIC: new_storage = CHECK_DECLSPEC_STATIC; break;
                case C_NAME_AUTO: new_storage = CHECK_DECLSPEC_AUTO; break;
                case C_NAME_REGISTER: new_storage = CHECK_DECLSPEC_REGISTER; break;
                case C_NAME_GNU_THREAD: new_thread = CHECK_DECLSPEC_GNU_THREAD; break;
                case C_NAME_THREAD_LOCAL: new_thread = CHECK_DECLSPEC_THREAD_LOCAL; break;
                case C_NAME_THREAD_LOCAL_C11: new_thread = CHECK_DECLSPEC_THREAD_LOCAL_C11; break;
                case C_NAME_CONSTEXPR: constexpr_token = constexpr_token == BUSTER_CHECK_NONE ? i : constexpr_token; break;
                // inline and _Noreturn, function specifiers, which repeat with a warning.
                case C_NAME_INLINE: case C_NAME_GNU_INLINE: case C_NAME_GNU_INLINE2: case C_NAME_NORETURN: break;
                default: known = false; break;
            }
        }
        else if (name == C_NAME_SHORT || name == C_NAME_LONG)
        {
            // A `long` after `long` makes `long long`, which another `long` doesn't take.
            new_width = name == C_NAME_SHORT ? CHECK_DECLSPEC_SHORT : width == CHECK_DECLSPEC_LONG ? CHECK_DECLSPEC_LONG_LONG : CHECK_DECLSPEC_LONG;
        }
        else if (name == C_NAME_SIGNED || name == C_NAME_GNU_SIGNED || name == C_NAME_GNU_SIGNED2 || name == C_NAME_UNSIGNED)
        {
            new_sign = name == C_NAME_UNSIGNED ? CHECK_DECLSPEC_UNSIGNED : CHECK_DECLSPEC_SIGNED;
        }
        else if (name == C_NAME_COMPLEX_C99 || name == C_NAME_GNU_COMPLEX || name == C_NAME_GNU_COMPLEX2)
        {
            complex_token = complex_token == BUSTER_CHECK_NONE ? i : complex_token;
        }
        else if (kind == PARSE_KIND_TYPE)
        {
            new_type = check_declspec_type_name(name, c23);
            known = new_type != CHECK_DECLSPEC_NONE;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] == PARSE_ROLE_SPECIFIER && type == CHECK_DECLSPEC_NONE &&
                 width == CHECK_DECLSPEC_NONE && sign == CHECK_DECLSPEC_NONE && complex_token == BUSTER_CHECK_NONE)
        {
            // A typedef name is a type only where no type specifier came before it; after one it is the declarator's name.
            new_type = CHECK_DECLSPEC_TYPE_NAME;
        }
        else
        {
            reading = false;
        }
        // A second specifier of a slot: the same one repeats (a warning), another conflicts, but `long` after `long`.
        const uint8_t conflict = new_type != CHECK_DECLSPEC_NONE && type != CHECK_DECLSPEC_NONE ? type :
                                 new_width != CHECK_DECLSPEC_NONE && width != CHECK_DECLSPEC_NONE && new_width != width && new_width != CHECK_DECLSPEC_LONG_LONG ? width :
                                 new_sign != CHECK_DECLSPEC_NONE && sign != CHECK_DECLSPEC_NONE && new_sign != sign ? sign :
                                 new_storage != CHECK_DECLSPEC_NONE && storage != CHECK_DECLSPEC_NONE && new_storage != storage ? storage :
                                 new_thread != CHECK_DECLSPEC_NONE && thread != CHECK_DECLSPEC_NONE && new_thread != thread ? thread : CHECK_DECLSPEC_NONE;
        if (reading && conflict != CHECK_DECLSPEC_NONE)
        {
            *result = (CheckDeclspecResult){ .token = i, .error = CHECK_DECLSPEC_ERROR_COMBINE, .first = conflict };
        }
        else if (reading)
        {
            type_token = new_type != CHECK_DECLSPEC_NONE ? i : type_token;
            type = new_type != CHECK_DECLSPEC_NONE ? new_type : type;
            width_token = new_width != CHECK_DECLSPEC_NONE && width == CHECK_DECLSPEC_NONE ? i : width_token;
            width = new_width != CHECK_DECLSPEC_NONE ? new_width : width;
            sign_token = new_sign != CHECK_DECLSPEC_NONE && sign == CHECK_DECLSPEC_NONE ? i : sign_token;
            sign = new_sign != CHECK_DECLSPEC_NONE ? new_sign : sign;
            storage_token = new_storage != CHECK_DECLSPEC_NONE && storage == CHECK_DECLSPEC_NONE ? i : storage_token;
            storage = new_storage != CHECK_DECLSPEC_NONE ? new_storage : storage;
            thread_token = new_thread != CHECK_DECLSPEC_NONE && thread == CHECK_DECLSPEC_NONE ? i : thread_token;
            thread = new_thread != CHECK_DECLSPEC_NONE ? new_thread : thread;
            i = next;
        }
    }
    // The list's end: what the type takes (DeclSpec::Finish), in clang's order.
    const bool finish = known && result->error == CHECK_DECLSPEC_ERROR_NONE;
    const bool integer = type == CHECK_DECLSPEC_NONE || type == CHECK_DECLSPEC_INT;
    const bool floating = type == CHECK_DECLSPEC_FLOAT || type == CHECK_DECLSPEC_DOUBLE || type == CHECK_DECLSPEC_FLOAT16 || type == CHECK_DECLSPEC_FLOAT128;
    const bool thread_first = thread_token < storage_token;
    if (finish && sign != CHECK_DECLSPEC_NONE && !integer && type != CHECK_DECLSPEC_INT128 && type != CHECK_DECLSPEC_CHAR && type != CHECK_DECLSPEC_BIT_INT)
    {
        *result = (CheckDeclspecResult){ .token = sign_token, .error = CHECK_DECLSPEC_ERROR_SIGN, .first = type };
    }
    else if (finish && width != CHECK_DECLSPEC_NONE && !integer && !(width == CHECK_DECLSPEC_LONG && type == CHECK_DECLSPEC_DOUBLE))
    {
        *result = (CheckDeclspecResult){ .token = width_token, .error = CHECK_DECLSPEC_ERROR_WIDTH, .first = width, .second = type };
    }
    else if (finish && complex_token != BUSTER_CHECK_NONE && type != CHECK_DECLSPEC_NONE && type != CHECK_DECLSPEC_INT && type != CHECK_DECLSPEC_CHAR && !floating)
    {
        *result = (CheckDeclspecResult){ .token = complex_token, .error = CHECK_DECLSPEC_ERROR_COMPLEX, .first = type };
    }
    else if (finish && thread != CHECK_DECLSPEC_NONE && storage != CHECK_DECLSPEC_NONE && storage != CHECK_DECLSPEC_EXTERN && storage != CHECK_DECLSPEC_STATIC)
    {
        // At the later of the two, naming the earlier.
        *result = (CheckDeclspecResult){ .token = thread_first ? storage_token : thread_token, .error = CHECK_DECLSPEC_ERROR_COMBINE, .first = thread_first ? thread : storage };
    }
    else if (finish && c23 && constexpr_token != BUSTER_CHECK_NONE && (thread != CHECK_DECLSPEC_NONE || storage == CHECK_DECLSPEC_EXTERN))
    {
        *result = (CheckDeclspecResult){ .token = constexpr_token, .error = CHECK_DECLSPEC_ERROR_COMBINE, .first = thread != CHECK_DECLSPEC_NONE ? thread : storage };
    }
    if (slots != NULL)
    {
        *slots = (CheckDeclspecSlots){
            .type_token = type_token,
            .type = type,
            .width = width,
            .sign = sign,
            .complex = complex_token != BUSTER_CHECK_NONE,
            .known = known,
        };
    }
    return known && result->error != CHECK_DECLSPEC_ERROR_NONE;
}

static bool check_declspec_error(const Parser* parser, uint32_t first, uint32_t end, CheckDeclspecResult* result)
{
    return check_declspec_scan(parser, first, end, BUSTER_CHECK_NONE, result, NULL);
}
