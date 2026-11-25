// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __PRETTY_FUNCTION__ on the direct path (#3592): the function's signature as clang prints it in C, made from the
// definition's tokens at its first use in a function and kept for the rest of it. glibc's assert passes it in every C
// mode, so a unit that keeps its asserts asks once a function that has one; nothing else here runs.
//
// clang's PredefinedExpr::ComputeName prints the return type with the name and the parameters' types where a
// declarator's name goes (TypePrinter's placeholder), each type as written: typedef names kept, a declarator's
// parentheses kept, a parameter adjusted (an array to a pointer to its element, with the brackets' qualifiers; a function
// typedef to a pointer to it; va_list, an array of one struct __va_list_tag, to a pointer to that), `(void)` for a
// prototype
// without parameters, `()` for a definition without one (before C23), and no storage class, function specifier or
// attribute: `void (*signal(int, void (*)(int)))(int)`. A declaration this doesn't print as clang does leaves the
// direct path: typeof, _Atomic, an unnamed or defined tag, a typedef of another array type as a parameter, an array
// bound that isn't an integer literal, an attribute in a declarator, one in a parameter but `unused`, and one that
// makes a type (vector_size, mode).
//
// The direct path's call graph has no recursion: a declarator is read from its name outward over its parentheses'
// levels, a parameter list waits in a queue until its function's declarator is read, and a type is printed by a
// stack of frames, one a parameter list being printed.

#ifndef BUSTER_C_DIRECT_PRETTY_INCLUDED
#define BUSTER_C_DIRECT_PRETTY_INCLUDED

#define BUSTER_FPRETTY_TYPES 96u        // a signature's types: its own, its parameters' and theirs
#define BUSTER_FPRETTY_DERIVATIONS 12u  // a type's pointers, arrays, functions and parentheses
#define BUSTER_FPRETTY_LEVELS 8u        // a declarator's nested parentheses
#define BUSTER_FPRETTY_POINTERS 16u     // a declarator's pointers
#define BUSTER_FPRETTY_LISTS 32u        // parameter lists
#define BUSTER_FPRETTY_FRAMES 16u       // parameter lists being printed, one inside another
#define BUSTER_FPRETTY_BYTES 4096u      // the printed signature

#define BUSTER_FPRETTY_POINTER 1u
#define BUSTER_FPRETTY_ARRAY 2u
#define BUSTER_FPRETTY_FUNCTION 3u
#define BUSTER_FPRETTY_PAREN 4u

#define BUSTER_FPRETTY_CONST 1u
#define BUSTER_FPRETTY_VOLATILE 2u
#define BUSTER_FPRETTY_RESTRICT 4u

// A derivation of a type, from its declarator's name outward: a pointer with its qualifiers, an array with its bound
// (UINT32_MAX without one) and, a parameter's, the qualifiers in its brackets, a function with its first parameter (a
// type's index, 0 for none), or a declarator's parentheses.
typedef struct FastPrettyDerivation FastPrettyDerivation;
struct FastPrettyDerivation
{
    uint8_t kind;
    uint8_t qualifiers;
    uint8_t prototyped;
    uint8_t variadic;
    uint32_t value;
};

// A type: its base (a keyword type's name, `struct ` and the like with a tag's name, or a typedef's name), the base's
// qualifiers and its derivations; a parameter links to the next of its list.
typedef struct FastPrettyType FastPrettyType;
struct FastPrettyType
{
    const char* keyword;
    uint32_t name;
    uint32_t next;
    uint8_t qualifiers;
    uint8_t count;
    uint8_t va_list;
    uint8_t function_typedef;
    FastPrettyDerivation derivations[BUSTER_FPRETTY_DERIVATIONS];
};

// A parameter list read after its declarator: its function's derivation, and its tokens between the parentheses.
typedef struct FastPrettyList FastPrettyList;
struct FastPrettyList
{
    uint32_t type;
    uint32_t derivation;
    uint32_t first;
    uint32_t end;
};

// A type being printed: past its placeholder, the derivation whose text comes next, and in a function's parameter list
// the next parameter (0 after the last) and whether one has been printed.
typedef struct FastPrettyFrame FastPrettyFrame;
struct FastPrettyFrame
{
    uint32_t type;
    uint32_t derivation;
    uint32_t parameter;
    uint8_t empty;
    uint8_t in_list;
    uint8_t printed;
};

typedef struct FastPretty FastPretty;
struct FastPretty
{
    FastCCState* cc;
    uint32_t pos;
    bool bad;
    uint32_t type_count;
    uint32_t list_count;
    uint32_t list_done;
    uint32_t length;
    FastPrettyType types[BUSTER_FPRETTY_TYPES];
    FastPrettyList lists[BUSTER_FPRETTY_LISTS];
    char text[BUSTER_FPRETTY_BYTES];
};

static void fast_pretty_fail(FastPretty* p)
{
    p->bad = true;
}

static uint8_t fast_pretty_kind(const FastPretty* p, uint32_t at, uint32_t end)
{
    return at < end ? p->cc->tk[at] : (uint8_t)FTOK_EOF;
}

// Whether the token at `at` is spelled `spelling` (a keyword token keeps its identifier).
static bool fast_pretty_spelled(const FastPretty* p, uint32_t at, const char* spelling)
{
    uint32_t length = 0;
    const char* text = fast_cc_ident_str(p->cc, p->cc->td[at], &length);
    return strlen(spelling) == length && memcmp(text, spelling, length) == 0;
}

// The file scope's binding of `id` while a function's scopes are open: the binding its first shadowing in this
// function's scopes saved, or the current one. Its symbol where that is a typedef, else 0.
static uint32_t fast_pretty_typedef(const FastCCState* cc, uint32_t id)
{
    uint32_t symbol = id != 0 && id < cc->env_cap ? cc->sym_cur[id] : 0u;
    for (uint32_t i = 0; i < cc->undo_count; i += 1)
    {
        if (cc->undo[i].is_tag == 0 && cc->undo[i].ident_id == id)
        {
            symbol = cc->undo[i].prev_sym;
            break;
        }
    }
    return symbol != 0 && cc->syms[symbol].kind == FSYM_TYPEDEF ? symbol : 0u;
}

// The token past the bracket that closes the one at `at`, or `end` without one.
static uint32_t fast_pretty_close(const FastPretty* p, uint32_t at, uint32_t end)
{
    uint32_t depth = 0;
    uint32_t result = end;
    for (uint32_t i = at; i < end; i += 1)
    {
        const uint8_t k = p->cc->tk[i];
        depth += (uint32_t)(k == FTOK_LPAREN || k == FTOK_LBRACKET || k == FTOK_LBRACE);
        depth -= (uint32_t)(k == FTOK_RPAREN || k == FTOK_RBRACKET || k == FTOK_RBRACE);
        if (depth == 0)
        {
            result = i + 1u;
            break;
        }
    }
    return result;
}

// `__attribute__((...))`s at p->pos, past them. In a parameter only `unused`; elsewhere any but those that make a
// type, which clang would print.
static void fast_pretty_attributes(FastPretty* p, uint32_t end, bool parameter)
{
    while (!p->bad && fast_pretty_kind(p, p->pos, end) == FTOK_KW_ATTRIBUTE)
    {
        const uint32_t open = p->pos + 1u;
        const uint32_t close = fast_pretty_close(p, open, end);
        const bool shaped = fast_pretty_kind(p, open, end) == FTOK_LPAREN && fast_pretty_kind(p, open + 1u, end) == FTOK_LPAREN && close <= end &&
                            p->cc->tk[close - 1u] == FTOK_RPAREN && p->cc->tk[close - 2u] == FTOK_RPAREN;
        if (!shaped)
        {
            fast_pretty_fail(p);
            break;
        }
        // Each attribute's name, at the list's own depth: after `((` and after each comma there.
        uint32_t depth = 0;
        bool name_next = true;
        for (uint32_t i = open + 2u; i + 2u < close && !p->bad; i += 1)
        {
            const uint8_t k = p->cc->tk[i];
            if (depth == 0 && name_next && (k == FTOK_IDENT || (k >= 64u && k <= FTOK_KW_FUNC)))
            {
                const bool unused = fast_pretty_spelled(p, i, "unused") || fast_pretty_spelled(p, i, "__unused__");
                const bool makes_type = fast_pretty_spelled(p, i, "vector_size") || fast_pretty_spelled(p, i, "__vector_size__") ||
                                        fast_pretty_spelled(p, i, "mode") || fast_pretty_spelled(p, i, "__mode__") ||
                                        fast_pretty_spelled(p, i, "ext_vector_type") || fast_pretty_spelled(p, i, "address_space") ||
                                        fast_pretty_spelled(p, i, "noderef") || fast_pretty_spelled(p, i, "btf_type_tag") ||
                                        fast_pretty_spelled(p, i, "annotate_type") || fast_pretty_spelled(p, i, "matrix_type");
                if ((parameter && !unused) || makes_type)
                {
                    fast_pretty_fail(p);
                }
            }
            name_next = depth == 0 && k == FTOK_COMMA;
            depth += (uint32_t)(k == FTOK_LPAREN);
            depth -= (uint32_t)(k == FTOK_RPAREN && depth > 0);
        }
        p->pos = close;
    }
}

// A keyword type's name as clang prints it, or NULL for a combination clang would refuse or print otherwise.
static const char* fast_pretty_builtin(uint8_t keyword, uint32_t sign, bool is_short, uint32_t longs, bool is_complex, bool c23)
{
    const char* name = NULL;
    const bool sized = is_short || longs != 0;
    if (is_complex)
    {
        name = sign != 0 || is_short ? NULL :
               keyword == FTOK_KW_FLOAT && longs == 0 ? "_Complex float" :
               keyword == FTOK_KW_DOUBLE && longs == 0 ? "_Complex double" :
               keyword == FTOK_KW_DOUBLE && longs == 1 ? "_Complex long double" : NULL;
    }
    else if (keyword == FTOK_KW_VOID || keyword == FTOK_KW_BOOL || keyword == FTOK_KW_FLOAT)
    {
        name = sign != 0 || sized ? NULL : keyword == FTOK_KW_VOID ? "void" : keyword == FTOK_KW_FLOAT ? "float" : c23 ? "bool" : "_Bool";
    }
    else if (keyword == FTOK_KW_DOUBLE)
    {
        name = sign != 0 || is_short || longs > 1 ? NULL : longs == 1 ? "long double" : "double";
    }
    else if (keyword == FTOK_KW_CHAR)
    {
        name = sized ? NULL : sign == 1 ? "signed char" : sign == 2 ? "unsigned char" : "char";
    }
    else if (keyword == FTOK_KW_INT128)
    {
        name = sized ? NULL : sign == 2 ? "unsigned __int128" : "__int128";
    }
    else if (keyword == FTOK_KW_INT || keyword == 0)
    {
        const bool unsigned_type = sign == 2;
        name = keyword == 0 && sign == 0 && !sized ? NULL :
               is_short && longs != 0 ? NULL :
               is_short ? (unsigned_type ? "unsigned short" : "short") :
               longs == 0 ? (unsigned_type ? "unsigned int" : "int") :
               longs == 1 ? (unsigned_type ? "unsigned long" : "long") :
               longs == 2 ? (unsigned_type ? "unsigned long long" : "long long") : NULL;
    }
    return name;
}

// The declaration specifiers at p->pos as the base of t. A parameter's take register as their one storage class; the
// function's take static, extern, inline and _Noreturn. An identifier is a typedef's name before any type specifier
// and the declarator's after one.
static void fast_pretty_specifiers(FastPretty* p, FastPrettyType* t, uint32_t end, bool parameter)
{
    FastCCState* cc = p->cc;
    uint32_t sign = 0;
    uint32_t longs = 0;
    bool is_short = false;
    bool is_complex = false;
    bool specified = false;
    uint8_t keyword = 0;
    bool done = false;
    while (!done && !p->bad)
    {
        const uint8_t k = fast_pretty_kind(p, p->pos, end);
        if (k == FTOK_KW_CONST || k == FTOK_KW_VOLATILE || k == FTOK_KW_RESTRICT)
        {
            t->qualifiers |= (uint8_t)(k == FTOK_KW_CONST ? BUSTER_FPRETTY_CONST : k == FTOK_KW_VOLATILE ? BUSTER_FPRETTY_VOLATILE : BUSTER_FPRETTY_RESTRICT);
            p->pos += 1u;
        }
        else if (k == FTOK_KW_STATIC || k == FTOK_KW_EXTERN || k == FTOK_KW_INLINE || k == FTOK_KW_NORETURN || k == FTOK_KW_REGISTER)
        {
            if (parameter != (k == FTOK_KW_REGISTER))
            {
                fast_pretty_fail(p);
            }
            p->pos += 1u;
        }
        else if (k == FTOK_KW_EXTENSION)
        {
            p->pos += 1u;
        }
        else if (k == FTOK_KW_ATTRIBUTE)
        {
            fast_pretty_attributes(p, end, parameter);
        }
        else if (k == FTOK_KW_SIGNED || k == FTOK_KW_UNSIGNED)
        {
            const uint32_t now = k == FTOK_KW_SIGNED ? 1u : 2u;
            if (sign != 0 && sign != now)
            {
                fast_pretty_fail(p);
            }
            sign = now;
            specified = true;
            p->pos += 1u;
        }
        else if (k == FTOK_KW_SHORT || k == FTOK_KW_LONG || k == FTOK_KW_COMPLEX)
        {
            is_short |= k == FTOK_KW_SHORT;
            longs += (uint32_t)(k == FTOK_KW_LONG);
            is_complex |= k == FTOK_KW_COMPLEX;
            specified = true;
            p->pos += 1u;
        }
        else if (k == FTOK_KW_VOID || k == FTOK_KW_CHAR || k == FTOK_KW_INT || k == FTOK_KW_BOOL || k == FTOK_KW_FLOAT || k == FTOK_KW_DOUBLE ||
                 k == FTOK_KW_INT128)
        {
            // float, double and __int128 have other spellings that clang prints otherwise (_Float32, __int128_t).
            const bool canonical = (k != FTOK_KW_FLOAT || fast_pretty_spelled(p, p->pos, "float")) &&
                                   (k != FTOK_KW_DOUBLE || fast_pretty_spelled(p, p->pos, "double")) &&
                                   (k != FTOK_KW_INT128 || fast_pretty_spelled(p, p->pos, "__int128"));
            if (keyword != 0 || t->keyword != NULL || t->name != 0 || !canonical)
            {
                fast_pretty_fail(p);
            }
            keyword = k;
            specified = true;
            p->pos += 1u;
        }
        else if (k == FTOK_KW_BUILTIN_VA_LIST)
        {
            if (specified)
            {
                fast_pretty_fail(p);
            }
            t->keyword = "__builtin_va_list";
            t->va_list = 1u;
            specified = true;
            p->pos += 1u;
        }
        else if (k == FTOK_KW_STRUCT || k == FTOK_KW_UNION || k == FTOK_KW_ENUM)
        {
            // A tag by its name alone: an unnamed or defined one prints otherwise.
            const bool named = fast_pretty_kind(p, p->pos + 1u, end) == FTOK_IDENT && fast_pretty_kind(p, p->pos + 2u, end) != FTOK_LBRACE;
            if (specified || !named)
            {
                fast_pretty_fail(p);
            }
            t->keyword = k == FTOK_KW_STRUCT ? "struct " : k == FTOK_KW_UNION ? "union " : "enum ";
            t->name = cc->td[p->pos + 1u];
            specified = true;
            p->pos += 2u;
        }
        else if (k == FTOK_IDENT && !specified && fast_pretty_typedef(cc, cc->td[p->pos]) != 0)
        {
            const uint32_t symbol = fast_pretty_typedef(cc, cc->td[p->pos]);
            const FastType type = cc->syms[symbol].type;
            t->name = cc->td[p->pos];
            t->va_list = (uint8_t)(type == cc->va_list_type);
            t->function_typedef = (uint8_t)ftype_is_func(type);
            // A typedef of another array type decays to its element as clang wrote the typedef, which isn't kept here.
            if (ftype_is_array(type) && type != cc->va_list_type)
            {
                fast_pretty_fail(p);
            }
            specified = true;
            p->pos += 1u;
        }
        else if (k == FTOK_KW_TYPEOF || k == FTOK_KW_ATOMIC || k == FTOK_KW_ALIGNAS || k == FTOK_KW_THREAD_LOCAL || k == FTOK_KW_IMAGINARY ||
                 k == FTOK_KW_FLOAT16 || k == FTOK_KW_FLOAT128 || k == FTOK_KW_TYPEDEF || k == FTOK_KW_AUTO || k == FTOK_LBRACKET)
        {
            fast_pretty_fail(p);
        }
        else
        {
            done = true;
        }
    }
    if (!p->bad && t->keyword == NULL && t->name == 0)
    {
        t->keyword = fast_pretty_builtin(keyword, sign, is_short, longs, is_complex, cc->pp->c23);
        if (t->keyword == NULL)
        {
            fast_pretty_fail(p);
        }
    }
    else if (!p->bad && (keyword != 0 || sign != 0 || is_short || longs != 0 || is_complex))
    {
        fast_pretty_fail(p);
    }
}

static void fast_pretty_derive(FastPretty* p, FastPrettyType* t, FastPrettyDerivation derivation)
{
    if (t->count < BUSTER_FPRETTY_DERIVATIONS)
    {
        t->derivations[t->count] = derivation;
        t->count += 1u;
    }
    else
    {
        fast_pretty_fail(p);
    }
}

// Whether a `(` whose next token is at `at` groups a declarator: a pointer, another group, an array, or a name that
// isn't a typedef's. A specifier, a typedef's name or `)` starts a parameter list.
static bool fast_pretty_groups(const FastPretty* p, uint32_t at, uint32_t end)
{
    const uint8_t k = fast_pretty_kind(p, at, end);
    return k == FTOK_STAR || k == FTOK_LPAREN || k == FTOK_LBRACKET || (k == FTOK_IDENT && fast_pretty_typedef(p->cc, p->cc->td[at]) == 0);
}

// The declarator at p->pos, abstract where it names nothing: its derivations from the name outward appended to t, and
// its name into *name. Down to the name (or the place one would have), each level's pointers and the parenthesis that
// opens the next; then outward, each level's suffixes, its pointers the last written first, and its `)`. A function's
// parameter list waits in p->lists.
static void fast_pretty_declarator(FastPretty* p, uint32_t type, uint32_t end, uint32_t* name)
{
    FastCCState* cc = p->cc;
    FastPrettyType* t = p->types + type;
    uint8_t pointers[BUSTER_FPRETTY_POINTERS];
    uint32_t pointer_count = 0;
    uint32_t level_first[BUSTER_FPRETTY_LEVELS + 1u];
    uint32_t levels = 0;
    level_first[0] = 0;
    bool descending = true;
    while (descending && !p->bad)
    {
        while (!p->bad && fast_pretty_kind(p, p->pos, end) == FTOK_STAR)
        {
            p->pos += 1u;
            uint8_t qualifiers = 0;
            uint8_t k = fast_pretty_kind(p, p->pos, end);
            while (k == FTOK_KW_CONST || k == FTOK_KW_VOLATILE || k == FTOK_KW_RESTRICT)
            {
                qualifiers |= (uint8_t)(k == FTOK_KW_CONST ? BUSTER_FPRETTY_CONST : k == FTOK_KW_VOLATILE ? BUSTER_FPRETTY_VOLATILE : BUSTER_FPRETTY_RESTRICT);
                p->pos += 1u;
                k = fast_pretty_kind(p, p->pos, end);
            }
            if (pointer_count == BUSTER_FPRETTY_POINTERS || k == FTOK_KW_ATTRIBUTE)
            {
                fast_pretty_fail(p);
                break;
            }
            pointers[pointer_count] = qualifiers;
            pointer_count += 1u;
        }
        const uint8_t k = fast_pretty_kind(p, p->pos, end);
        if (k == FTOK_IDENT)
        {
            *name = cc->td[p->pos];
            p->pos += 1u;
            descending = false;
        }
        else if (k == FTOK_LPAREN && fast_pretty_groups(p, p->pos + 1u, end))
        {
            if (levels == BUSTER_FPRETTY_LEVELS)
            {
                fast_pretty_fail(p);
            }
            else
            {
                levels += 1u;
                level_first[levels] = pointer_count;
                p->pos += 1u;
            }
        }
        else
        {
            descending = false;
        }
    }
    uint32_t level = levels;
    bool ascending = true;
    while (ascending && !p->bad)
    {
        bool suffix = true;
        while (suffix && !p->bad)
        {
            const uint8_t k = fast_pretty_kind(p, p->pos, end);
            if (k == FTOK_LBRACKET)
            {
                p->pos += 1u;
                uint8_t qualifiers = 0;
                uint8_t q = fast_pretty_kind(p, p->pos, end);
                while (q == FTOK_KW_CONST || q == FTOK_KW_VOLATILE || q == FTOK_KW_RESTRICT || q == FTOK_KW_STATIC)
                {
                    qualifiers |= (uint8_t)(q == FTOK_KW_CONST ? BUSTER_FPRETTY_CONST : q == FTOK_KW_VOLATILE ? BUSTER_FPRETTY_VOLATILE :
                                            q == FTOK_KW_RESTRICT ? BUSTER_FPRETTY_RESTRICT : 0u);
                    p->pos += 1u;
                    q = fast_pretty_kind(p, p->pos, end);
                }
                // An integer literal's bound, printed as its value; none prints `[]`. Anything else, a VLA's
                // expression among them, clang prints as an expression or a computed value.
                uint32_t bound = UINT32_MAX;
                const bool bounded = (q == FTOK_INT_LIT || q == FTOK_INT_U32) && fast_pretty_kind(p, p->pos + 1u, end) == FTOK_RBRACKET;
                if (bounded)
                {
                    bound = cc->td[p->pos];
                    p->pos += 1u;
                }
                if (fast_pretty_kind(p, p->pos, end) != FTOK_RBRACKET || (bounded && bound == UINT32_MAX))
                {
                    fast_pretty_fail(p);
                }
                p->pos += 1u;
                fast_pretty_derive(p, t, (FastPrettyDerivation){ .kind = BUSTER_FPRETTY_ARRAY, .qualifiers = qualifiers, .value = bound });
            }
            else if (k == FTOK_LPAREN)
            {
                const uint32_t close = fast_pretty_close(p, p->pos, end);
                if (close > end || p->list_count == BUSTER_FPRETTY_LISTS || cc->tk[close - 1u] != FTOK_RPAREN)
                {
                    fast_pretty_fail(p);
                }
                else
                {
                    p->lists[p->list_count] = (FastPrettyList){ .type = type, .derivation = t->count, .first = p->pos + 1u, .end = close - 1u };
                    p->list_count += 1u;
                    fast_pretty_derive(p, t, (FastPrettyDerivation){ .kind = BUSTER_FPRETTY_FUNCTION });
                    p->pos = close;
                }
            }
            else
            {
                suffix = false;
            }
        }
        const uint32_t level_end = level == levels ? pointer_count : level_first[level + 1u];
        for (uint32_t i = level_end; i > level_first[level] && !p->bad; i -= 1)
        {
            fast_pretty_derive(p, t, (FastPrettyDerivation){ .kind = BUSTER_FPRETTY_POINTER, .qualifiers = pointers[i - 1u] });
        }
        if (level == 0)
        {
            ascending = false;
        }
        else if (fast_pretty_kind(p, p->pos, end) == FTOK_RPAREN)
        {
            p->pos += 1u;
            fast_pretty_derive(p, t, (FastPrettyDerivation){ .kind = BUSTER_FPRETTY_PAREN });
            level -= 1u;
        }
        else
        {
            fast_pretty_fail(p);
        }
    }
}

// A parameter's type as its function has it: an array to a pointer to its element with the brackets' qualifiers (the
// parentheses around it go), a typedef of a function type to a pointer to it, and va_list to a pointer to its one
// struct __va_list_tag.
static void fast_pretty_adjust(FastPretty* p, FastPrettyType* t)
{
    uint32_t first = 0;
    while (first < t->count && t->derivations[first].kind == BUSTER_FPRETTY_PAREN)
    {
        first += 1u;
    }
    if (first < t->count && t->derivations[first].kind == BUSTER_FPRETTY_ARRAY)
    {
        const uint8_t qualifiers = t->derivations[first].qualifiers;
        memmove(t->derivations + 1, t->derivations + first + 1u, (t->count - first - 1u) * sizeof(FastPrettyDerivation));
        t->count = (uint8_t)(t->count - first);
        t->derivations[0] = (FastPrettyDerivation){ .kind = BUSTER_FPRETTY_POINTER, .qualifiers = qualifiers };
    }
    else if (first < t->count && t->derivations[first].kind == BUSTER_FPRETTY_FUNCTION)
    {
        // A parameter declared as a function (`int g(int)`) leaves the direct path before any use of its function's
        // name, in the declaration's own parsing; one this would print can't be tested.
        fast_pretty_fail(p);
    }
    else if (first == t->count && t->va_list != 0)
    {
        if (first != 0 || t->qualifiers != 0)
        {
            fast_pretty_fail(p);
        }
        t->keyword = "struct __va_list_tag";
        t->name = 0;
        t->count = 1u;
        t->derivations[0] = (FastPrettyDerivation){ .kind = BUSTER_FPRETTY_POINTER };
    }
    else if (first == t->count && t->function_typedef != 0)
    {
        if (first != 0)
        {
            fast_pretty_fail(p);
        }
        t->count = 1u;
        t->derivations[0] = (FastPrettyDerivation){ .kind = BUSTER_FPRETTY_POINTER };
    }
}

// Whether every parenthesis of t groups a pointer against an array or function after it. clang keeps any other's
// (`int (*)` for `int (*p)`) and the full pipeline doesn't, so a type with one leaves the direct path.
static bool fast_pretty_parentheses_needed(const FastPrettyType* t, uint32_t first)
{
    bool needed = true;
    for (uint32_t i = first; i < t->count; i += 1)
    {
        if (t->derivations[i].kind == BUSTER_FPRETTY_PAREN)
        {
            const uint8_t inner = i > first ? t->derivations[i - 1u].kind : (uint8_t)0u;
            const uint8_t outer = i + 1u < t->count ? t->derivations[i + 1u].kind : (uint8_t)0u;
            needed &= inner == BUSTER_FPRETTY_POINTER && (outer == BUSTER_FPRETTY_ARRAY || outer == BUSTER_FPRETTY_FUNCTION);
        }
    }
    return needed;
}

static uint32_t fast_pretty_new(FastPretty* p)
{
    uint32_t index = 0;
    if (p->type_count < BUSTER_FPRETTY_TYPES)
    {
        index = p->type_count;
        p->type_count += 1u;
        p->types[index] = (FastPrettyType){ 0 };
    }
    else
    {
        fast_pretty_fail(p);
    }
    return index;
}

// The parameter lists in the queue, each parameter's declaration in its turn (a function's in it join the queue):
// `()` is a prototype only in C23, `(void)` one without parameters, and `...` ends a list.
static void fast_pretty_lists(FastPretty* p)
{
    FastCCState* cc = p->cc;
    while (p->list_done < p->list_count && !p->bad)
    {
        const FastPrettyList list = p->lists[p->list_done];
        p->list_done += 1u;
        FastPrettyDerivation* function = p->types[list.type].derivations + list.derivation;
        function->prototyped = (uint8_t)(list.first != list.end || cc->pp->c23);
        const bool void_list = list.end == list.first + 1u && cc->tk[list.first] == FTOK_KW_VOID;
        uint32_t at = void_list ? list.end : list.first;
        uint32_t last = 0;
        while (at < list.end && !p->bad)
        {
            // The parameter's tokens: to the comma or the list's end at its own depth.
            uint32_t stop = at;
            uint32_t depth = 0;
            while (stop < list.end && (depth != 0 || cc->tk[stop] != FTOK_COMMA))
            {
                const uint8_t k = cc->tk[stop];
                depth += (uint32_t)(k == FTOK_LPAREN || k == FTOK_LBRACKET);
                depth -= (uint32_t)((k == FTOK_RPAREN || k == FTOK_RBRACKET) && depth > 0);
                stop += 1u;
            }
            if (cc->tk[at] == FTOK_ELLIPSIS)
            {
                function->variadic = 1u;
                if (stop != at + 1u || stop != list.end)
                {
                    fast_pretty_fail(p);
                }
            }
            else
            {
                const uint32_t parameter = fast_pretty_new(p);
                p->pos = at;
                uint32_t name = 0;
                if (!p->bad)
                {
                    fast_pretty_specifiers(p, p->types + parameter, stop, true);
                    fast_pretty_declarator(p, parameter, stop, &name);
                    fast_pretty_attributes(p, stop, true);
                    fast_pretty_adjust(p, p->types + parameter);
                    if (!fast_pretty_parentheses_needed(p->types + parameter, 0))
                    {
                        fast_pretty_fail(p);
                    }
                }
                if (p->pos != stop)
                {
                    fast_pretty_fail(p);
                }
                if (last == 0)
                {
                    function = p->types[list.type].derivations + list.derivation;
                    function->value = parameter;
                }
                else
                {
                    p->types[last].next = parameter;
                }
                last = parameter;
            }
            at = stop + (uint32_t)(stop < list.end);
        }
    }
}

static void fast_pretty_put(FastPretty* p, const char* text, uint32_t length)
{
    if (p->length + length < BUSTER_FPRETTY_BYTES)
    {
        memcpy(p->text + p->length, text, length);
        p->length += length;
    }
    else
    {
        fast_pretty_fail(p);
    }
}

static void fast_pretty_putc(FastPretty* p, char c)
{
    fast_pretty_put(p, &c, 1u);
}

// Qualifiers as clang's AppendTypeQualList writes them, with a space after them where `space`.
static void fast_pretty_qualifiers(FastPretty* p, uint8_t qualifiers, bool space)
{
    bool written = false;
    if ((qualifiers & BUSTER_FPRETTY_CONST) != 0)
    {
        fast_pretty_put(p, "const", 5u);
        written = true;
    }
    if ((qualifiers & BUSTER_FPRETTY_VOLATILE) != 0)
    {
        fast_pretty_put(p, written ? " volatile" : "volatile", written ? 9u : 8u);
        written = true;
    }
    if ((qualifiers & BUSTER_FPRETTY_RESTRICT) != 0)
    {
        fast_pretty_put(p, written ? " restrict" : "restrict", written ? 9u : 8u);
        written = true;
    }
    if (written && space)
    {
        fast_pretty_putc(p, ' ');
    }
}

static uint8_t fast_pretty_derivation_kind(const FastPrettyType* t, uint32_t i)
{
    return i < t->count ? t->derivations[i].kind : (uint8_t)0u;
}

// What TypePrinter prints of t before the placeholder: the base, then each derivation's part from the innermost out.
// Whether the placeholder is empty below a derivation follows from the one above: a pointer's and a function's inner
// type print as if it weren't.
static void fast_pretty_before(FastPretty* p, const FastPrettyType* t, bool empty)
{
    bool empties[BUSTER_FPRETTY_DERIVATIONS + 1u];
    empties[0] = empty;
    for (uint32_t i = 0; i < t->count; i += 1)
    {
        const uint8_t kind = t->derivations[i].kind;
        empties[i + 1u] = (kind == BUSTER_FPRETTY_ARRAY || kind == BUSTER_FPRETTY_PAREN) && empties[i];
    }
    fast_pretty_qualifiers(p, t->qualifiers, true);
    if (t->keyword != NULL)
    {
        fast_pretty_put(p, t->keyword, (uint32_t)strlen(t->keyword));
    }
    if (t->name != 0)
    {
        uint32_t length = 0;
        const char* name = fast_cc_ident_str(p->cc, t->name, &length);
        fast_pretty_put(p, name, length);
    }
    if (!empties[t->count])
    {
        fast_pretty_putc(p, ' ');
    }
    for (uint32_t i = t->count; i > 0; i -= 1)
    {
        const FastPrettyDerivation* d = t->derivations + (i - 1u);
        const uint8_t inner = fast_pretty_derivation_kind(t, i);
        if (d->kind == BUSTER_FPRETTY_POINTER)
        {
            if (inner == BUSTER_FPRETTY_ARRAY)
            {
                fast_pretty_putc(p, '(');
            }
            fast_pretty_putc(p, '*');
            fast_pretty_qualifiers(p, d->qualifiers, !empties[i - 1u]);
        }
        else if (d->kind == BUSTER_FPRETTY_FUNCTION && !empties[i - 1u])
        {
            fast_pretty_putc(p, '(');
        }
        else if (d->kind == BUSTER_FPRETTY_PAREN && !empties[i - 1u] && inner != BUSTER_FPRETTY_FUNCTION)
        {
            fast_pretty_putc(p, '(');
        }
    }
}

static void fast_pretty_decimal(FastPretty* p, uint32_t value)
{
    char digits[10];
    uint32_t count = 0;
    uint32_t rest = value;
    do
    {
        digits[count] = (char)('0' + rest % 10u);
        count += 1u;
        rest /= 10u;
    } while (rest != 0);
    while (count > 0)
    {
        count -= 1u;
        fast_pretty_putc(p, digits[count]);
    }
}

// Prints the type `root` with a placeholder of `length` bytes: what comes before it, the placeholder, and then each
// derivation's part from the outermost in, a function's parameters each printed in a frame of its own.
static void fast_pretty_type(FastPretty* p, uint32_t root, const char* placeholder, uint32_t length)
{
    FastPrettyFrame frames[BUSTER_FPRETTY_FRAMES];
    uint32_t frame_count = 1u;
    frames[0] = (FastPrettyFrame){ .type = root, .empty = (uint8_t)(length == 0) };
    fast_pretty_before(p, p->types + root, length == 0);
    fast_pretty_put(p, placeholder, length);
    while (frame_count > 0 && !p->bad)
    {
        FastPrettyFrame* f = frames + (frame_count - 1u);
        const FastPrettyType* t = p->types + f->type;
        if (f->in_list != 0)
        {
            const FastPrettyDerivation* function = t->derivations + f->derivation;
            if (f->parameter != 0)
            {
                // The next parameter, in a frame of its own: its text before an empty placeholder now, the rest as
                // its frame goes.
                const uint32_t parameter = f->parameter;
                if (f->printed != 0)
                {
                    fast_pretty_put(p, ", ", 2u);
                }
                f->printed = 1u;
                f->parameter = p->types[parameter].next;
                if (frame_count == BUSTER_FPRETTY_FRAMES)
                {
                    fast_pretty_fail(p);
                }
                else
                {
                    frames[frame_count] = (FastPrettyFrame){ .type = parameter, .empty = 1u };
                    frame_count += 1u;
                    fast_pretty_before(p, p->types + parameter, true);
                }
            }
            else
            {
                if (function->variadic != 0)
                {
                    fast_pretty_put(p, f->printed != 0 ? ", ..." : "...", f->printed != 0 ? 5u : 3u);
                }
                else if (f->printed == 0 && function->prototyped != 0)
                {
                    fast_pretty_put(p, "void", 4u);
                }
                fast_pretty_putc(p, ')');
                f->in_list = 0;
                f->derivation += 1u;
                f->empty = 0;
            }
        }
        else if (f->derivation == t->count)
        {
            frame_count -= 1u;
        }
        else
        {
            const FastPrettyDerivation* d = t->derivations + f->derivation;
            const uint8_t inner = fast_pretty_derivation_kind(t, f->derivation + 1u);
            if (d->kind == BUSTER_FPRETTY_POINTER)
            {
                if (inner == BUSTER_FPRETTY_ARRAY)
                {
                    fast_pretty_putc(p, ')');
                }
                f->empty = 0;
                f->derivation += 1u;
            }
            else if (d->kind == BUSTER_FPRETTY_ARRAY)
            {
                fast_pretty_putc(p, '[');
                if (d->value != UINT32_MAX)
                {
                    fast_pretty_decimal(p, d->value);
                }
                fast_pretty_putc(p, ']');
                f->derivation += 1u;
            }
            else if (d->kind == BUSTER_FPRETTY_FUNCTION)
            {
                if (f->empty == 0)
                {
                    fast_pretty_putc(p, ')');
                }
                fast_pretty_putc(p, '(');
                f->in_list = 1u;
                f->printed = 0;
                f->parameter = d->value;
            }
            else
            {
                if (f->empty == 0 && inner != BUSTER_FPRETTY_FUNCTION)
                {
                    fast_pretty_putc(p, ')');
                }
                f->derivation += 1u;
            }
        }
    }
}

// The current function's __PRETTY_FUNCTION__, its length into *out_length: printed at its first use from the
// definition's tokens, cc->cur_func_decl_pos to cc->cur_func_body_pos, and kept for the rest of the function. A
// definition it can't print as clang does leaves the direct path, and gives "".
static __attribute__((cold, noinline)) const char* fast_cc_pretty_function(FastCCState* cc, uint32_t* out_length)
{
    if (cc->cur_func_pretty == NULL && !cc->bailout)
    {
        FastPretty* p = (FastPretty*)fast_arena_alloc(cc->scratch, sizeof(FastPretty), 64);
        char* text = NULL;
        uint32_t text_length = 0;
        if (p != NULL)
        {
            p->cc = cc;
            p->pos = cc->cur_func_decl_pos;
            p->bad = cc->cur_func_name_id == 0 || cc->cur_func_body_pos <= cc->cur_func_decl_pos;
            p->type_count = 1u;
            p->list_count = 0;
            p->list_done = 0;
            p->length = 0;
            const uint32_t end = cc->cur_func_body_pos;
            const uint32_t function = fast_pretty_new(p);
            uint32_t name = 0;
            if (!p->bad)
            {
                fast_pretty_specifiers(p, p->types + function, end, false);
                fast_pretty_declarator(p, function, end, &name);
                fast_pretty_attributes(p, end, false);
                // The declaration ends at the body: K&R's parameter declarations, or an asm label, leave.
                if (p->pos != end)
                {
                    fast_pretty_fail(p);
                }
                fast_pretty_lists(p);
            }
            // The function's own derivation: past the parentheses around its name, the first; its return type is the
            // rest.
            const FastPrettyType* t = p->types + function;
            uint32_t own = 0;
            while (own < t->count && t->derivations[own].kind == BUSTER_FPRETTY_PAREN)
            {
                own += 1u;
            }
            // A definition's `()` before C23 is `()` in clang, where the full pipeline prints a declaration's earlier
            // prototype; parentheses around the name aren't printed by clang, and may be by the full pipeline.
            const bool empty_list = own < t->count && t->derivations[own].prototyped == 0;
            if (name != cc->cur_func_name_id || own != 0 || t->count == 0 || t->derivations[own].kind != BUSTER_FPRETTY_FUNCTION ||
                !fast_pretty_parentheses_needed(t, own + 1u) || (empty_list && cc->cur_func_declared != 0))
            {
                fast_pretty_fail(p);
            }
            if (!p->bad)
            {
                // The name and its parameters, each type as it is printed alone, then the return type around them.
                uint32_t name_length = 0;
                const char* name_text = fast_cc_ident_str(cc, name, &name_length);
                fast_pretty_put(p, name_text, name_length);
                fast_pretty_putc(p, '(');
                const FastPrettyDerivation own_function = t->derivations[own];
                bool printed = false;
                for (uint32_t parameter = own_function.value; parameter != 0 && !p->bad; parameter = p->types[parameter].next)
                {
                    if (printed)
                    {
                        fast_pretty_put(p, ", ", 2u);
                    }
                    printed = true;
                    fast_pretty_type(p, parameter, "", 0);
                }
                if (own_function.variadic != 0)
                {
                    fast_pretty_put(p, printed ? ", ..." : "...", printed ? 5u : 3u);
                }
                else if (!printed && own_function.prototyped != 0)
                {
                    fast_pretty_put(p, "void", 4u);
                }
                fast_pretty_putc(p, ')');
            }
            const uint32_t returned = fast_pretty_new(p);
            if (!p->bad)
            {
                char* proto = (char*)fast_arena_alloc(cc->scratch, p->length + 1u, 1);
                if (proto == NULL)
                {
                    fast_pretty_fail(p);
                }
                else
                {
                    const uint32_t proto_length = p->length;
                    memcpy(proto, p->text, proto_length);
                    FastPrettyType* r = p->types + returned;
                    *r = p->types[function];
                    r->count = (uint8_t)(t->count - own - 1u);
                    memmove(r->derivations, p->types[function].derivations + own + 1u, r->count * sizeof(FastPrettyDerivation));
                    p->length = 0;
                    fast_pretty_type(p, returned, proto, proto_length);
                }
            }
            if (!p->bad)
            {
                text = (char*)fast_arena_alloc(cc->arena, p->length + 1u, 1);
                if (text != NULL)
                {
                    memcpy(text, p->text, p->length);
                    text[p->length] = '\0';
                    text_length = p->length;
                }
            }
        }
        if (text == NULL)
        {
            fast_cc_bail(cc, "__PRETTY_FUNCTION__ printed otherwise");
        }
        cc->cur_func_pretty = text;
        cc->cur_func_pretty_length = text_length;
    }
    *out_length = cc->cur_func_pretty_length;
    return cc->cur_func_pretty != NULL ? cc->cur_func_pretty : "";
}

#endif
