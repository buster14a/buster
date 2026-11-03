// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Generates c_check_attribute_subjects.inc (#147): the subjects clang lets each attribute apply to in C, read from clang's
// generated attribute code, `clang-tblgen --gen-clang-attr-parsed-attr-impl` of its Attr.td. Each ParsedAttrInfo's
// diagAppertainsToDecl there is one shape: `if (!term && !term ...) { S.Diag(Attr.getLoc(), diag::DIAG) << Attr <<
// Attr.isRegularKeywordAttribute() << "SUBJECTS"; return false; } return true;`. Each term is mapped to the C
// declarations it takes (BUSTER_CHECK_SUBJECT_*, c_check_attribute.c), a term of no C declaration (Objective-C's, C++'s,
// HLSL's and the like) to none, and an unknown term is a failure. Each GNU and C23 spelling of the attribute is a row:
//
//   BUSTER_CHECK_SUBJECT_GNU("name", subjects, error, "SUBJECTS")
//   BUSTER_CHECK_SUBJECT_C23("scope::name", subjects, error, "SUBJECTS")
//
// each list in its spellings' byte order. `error` is clang's err_attribute_wrong_decl_type_str, else its warning.
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_attribute_subjects.c -o build/generate_attribute_subjects
//   build/generate_attribute_subjects --self-test
//   build/generate_attribute_subjects parsed.inc "SOURCE DESCRIPTION" > c_check_attribute_subjects.inc
//
// It succeeds only when it has read the whole input, mapped every term and written and flushed the whole table.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUBJECT_FUNCTION 0x1u
#define SUBJECT_VARIABLE 0x2u
#define SUBJECT_PARAMETER 0x4u
#define SUBJECT_FIELD 0x8u
#define SUBJECT_STRUCT 0x10u
#define SUBJECT_UNION 0x20u
#define SUBJECT_ENUM 0x40u
#define SUBJECT_TYPEDEF 0x80u
#define SUBJECT_ENUMERATOR 0x100u
#define SUBJECT_LABEL 0x200u
#define SUBJECT_PROTOTYPED 0x400u
#define SUBJECT_FUNCTION_LIKE 0x800u
#define SUBJECT_FUNCTION_POINTER 0x1000u
#define SUBJECT_GLOBAL 0x2000u
#define SUBJECT_SHARED 0x4000u
#define SUBJECT_EXTERNAL_GLOBAL 0x8000u
#define SUBJECT_LOCAL 0x10000u
#define SUBJECT_NON_PARAMETER_VARIABLE 0x20000u
#define SUBJECT_THREAD_LOCAL 0x40000u
#define SUBJECT_INLINE 0x80000u
#define SUBJECT_BIT_FIELD 0x100000u
#define SUBJECT_NON_BIT_FIELD 0x200000u
#define SUBJECT_ALL_DECLARATIONS (SUBJECT_FUNCTION | SUBJECT_VARIABLE | SUBJECT_PARAMETER | SUBJECT_FIELD | SUBJECT_STRUCT | SUBJECT_UNION | SUBJECT_ENUM | \
                                  SUBJECT_TYPEDEF | SUBJECT_ENUMERATOR | SUBJECT_LABEL)

typedef struct Term Term;
struct Term
{
    const char* text;
    uint32_t subjects;
};

// Each term of clang's predicates and the C declarations it takes. A C declaration is one of FunctionDecl, VarDecl (with
// ParmVarDecl), FieldDecl, RecordDecl (a struct or a union), EnumDecl, TypedefNameDecl, EnumConstantDecl and LabelDecl;
// Attr.td's subsets of them (GlobalVar and the like) are bits of their own, which a declaration has where it is one.
static const Term terms[] = {
    { "isa<FunctionDecl>(D)", SUBJECT_FUNCTION },
    { "isa<VarDecl>(D)", SUBJECT_VARIABLE },
    { "isa<ParmVarDecl>(D)", SUBJECT_PARAMETER },
    { "isa<FieldDecl>(D)", SUBJECT_FIELD },
    { "isa<RecordDecl>(D)", SUBJECT_STRUCT | SUBJECT_UNION },
    { "isa<TagDecl>(D)", SUBJECT_STRUCT | SUBJECT_UNION | SUBJECT_ENUM },
    { "isa<TypeDecl>(D)", SUBJECT_STRUCT | SUBJECT_UNION | SUBJECT_ENUM | SUBJECT_TYPEDEF },
    { "isa<NamedDecl>(D)", SUBJECT_ALL_DECLARATIONS },
    { "isa<TypedefNameDecl>(D)", SUBJECT_TYPEDEF },
    { "isa<EnumDecl>(D)", SUBJECT_ENUM },
    { "isa<EnumConstantDecl>(D)", SUBJECT_ENUMERATOR },
    { "isa<LabelDecl>(D)", SUBJECT_LABEL },
    { "isStruct(D)", SUBJECT_STRUCT },
    { "isHasFunctionProto(D)", SUBJECT_PROTOTYPED },
    { "isFunctionLike(D)", SUBJECT_FUNCTION_LIKE },
    { "isFunctionPointer(D)", SUBJECT_FUNCTION_POINTER },
    { "isGlobalVar(D)", SUBJECT_GLOBAL },
    { "isNonLocalVar(D)", SUBJECT_GLOBAL },
    { "isSharedVar(D)", SUBJECT_SHARED },
    { "isNonTLSGlobalVar(D)", SUBJECT_SHARED },
    { "isExternalGlobalVar(D)", SUBJECT_EXTERNAL_GLOBAL },
    { "isLocalVar(D)", SUBJECT_LOCAL },
    { "isNonParmVar(D)", SUBJECT_NON_PARAMETER_VARIABLE },
    { "isTLSVar(D)", SUBJECT_THREAD_LOCAL },
    { "isInlineFunction(D)", SUBJECT_INLINE },
    { "isBitField(D)", SUBJECT_BIT_FIELD },
    { "isNonBitField(D)", SUBJECT_NON_BIT_FIELD },
    // Of no C declaration.
    { "isa<ObjCMethodDecl>(D)", 0 },
    { "isa<CXXRecordDecl>(D)", 0 },
    { "isa<ObjCInterfaceDecl>(D)", 0 },
    { "isa<ObjCPropertyDecl>(D)", 0 },
    { "isa<BlockDecl>(D)", 0 },
    { "isa<ObjCProtocolDecl>(D)", 0 },
    { "isa<CXXMethodDecl>(D)", 0 },
    { "isHLSLEntry(D)", 0 },
    { "isa<NamespaceDecl>(D)", 0 },
    { "isHLSLBufferObj(D)", 0 },
    { "isImplicitObjectParameter(D)", 0 },
    { "isClassTmpl(D)", 0 },
    { "isFunctionTmpl(D)", 0 },
    { "isa<ObjCImplDecl>(D)", 0 },
    { "isHLSLInputBuiltin(D)", 0 },
    { "isHLSLOutputBuiltin(D)", 0 },
    { "isObjCInstanceMethod(D)", 0 },
    { "isVarTmpl(D)", 0 },
    { "isNonStaticCXXMethod(D)", 0 },
    { "isa<ObjCCategoryDecl>(D)", 0 },
    { "isNonStaticNonConstCXXMethod(D)", 0 },
    { "isa<ObjCImplementationDecl>(D)", 0 },
    { "isa<EmptyDecl>(D)", 0 },
    { "isa<BindingDecl>(D)", 0 },
    { "isa<ObjCIvarDecl>(D)", 0 },
    { "isa<UsingDecl>(D)", 0 },
    { "isa<UnresolvedUsingTypenameDecl>(D)", 0 },
    { "isa<UnresolvedUsingValueDecl>(D)", 0 },
};

// A spelling's facts (BUSTER_CHECK_FACT_GNU and BUSTER_CHECK_FACT_C23 rows), from its ParsedAttrInfo: GCC knows the
// attribute (IsKnownToGCC), it isn't C's (acceptsLangOpts false in C), clang's generated handler applies it with nothing
// more (handleDeclAttribute, Attr.td's SimpleHandler), it is a type's or a statement's, its arguments are its own
// parsing's, its last argument is variadic (OptArgs 15), or clang ignores it whatever it is on (an IgnoredAttribute).
#define FACT_GCC 0x1u
#define FACT_NOT_C 0x2u
#define FACT_SIMPLE 0x4u
#define FACT_TYPE 0x8u
#define FACT_STATEMENT 0x10u
#define FACT_CUSTOM_PARSING 0x20u
#define FACT_VARIADIC 0x40u
#define FACT_IGNORED 0x80u

typedef struct Fact Fact;
struct Fact
{
    char spelling[96];
    bool standard;
    uint32_t flags;
    uint32_t minimum;
    uint32_t maximum;
};

enum
{
    FACTS = 4096,
};

static Fact facts[FACTS];
static uint32_t fact_count;

// The target predicates an attribute's existsInTarget can ask, and their answers for x86-64 Linux.
typedef struct Predicate Predicate;
struct Predicate
{
    const char* text;
    bool value;
};

static const Predicate predicates[] = {
    { "true", true },
    { "T.getArch() == llvm::Triple::x86_64", true },
    { "Target.getTriple().hasDLLImportExport()", false },
    { "Target.hasMicrosoftRecordLayout()", false },
    { "Target.getCXXABI().isMicrosoft()", false },
    { "T.getOS() == llvm::Triple::Win32", false },
    { "Target.supportsIFunc()", true },
    { "Target.hasMustTail()", true },
    { "Target.getTriple().isWindowsArm64EC()", false },
};

typedef struct Row Row;
struct Row
{
    char spelling[96];
    bool standard;
    uint32_t subjects;
    bool error;
    char message[192];
};

enum
{
    ROWS = 4096,
};

static Row rows[ROWS];
static uint32_t row_count;

// The input's text, terminated.
static char* read_all(const char* path, size_t* length)
{
    FILE* file = fopen(path, "rb");
    char* result = NULL;
    *length = 0;
    if (file != NULL)
    {
        const bool sought = fseek(file, 0, SEEK_END) == 0;
        const long size = sought ? ftell(file) : -1;
        const bool rewound = size >= 0 && fseek(file, 0, SEEK_SET) == 0;
        char* text = rewound ? malloc((size_t)size + 1) : NULL;
        const bool read = text != NULL && fread(text, 1, (size_t)size, file) == (size_t)size;
        if (read)
        {
            text[size] = 0;
            *length = (size_t)size;
            result = text;
        }
        else
        {
            free(text);
        }
        fclose(file);
    }
    return result;
}

// The subjects of `term` (`length` bytes), or false where it is no term of the table.
static bool term_subjects(const char* term, size_t length, uint32_t* subjects)
{
    bool found = false;
    for (size_t t = 0; !found && t < sizeof(terms) / sizeof(terms[0]); t += 1)
    {
        found = strlen(terms[t].text) == length && memcmp(terms[t].text, term, length) == 0;
        *subjects = found ? terms[t].subjects : *subjects;
    }
    return found;
}

// The subjects of a predicate `!a && !b ...` from `begin` to `end`; false where a term is unknown, reported.
static bool predicate_subjects(const char* begin, const char* end, uint32_t* subjects)
{
    bool valid = true;
    *subjects = 0;
    const char* at = begin;
    while (valid && at < end)
    {
        while (at < end && (*at == ' ' || *at == '\n' || *at == '&'))
        {
            at += 1;
        }
        valid = at < end && *at == '!';
        const char* term = at + 1;
        const char* stop = term;
        while (valid && stop < end && *stop != ' ' && *stop != '\n' && *stop != '&')
        {
            stop += 1;
        }
        uint32_t subject = 0;
        valid = valid && term_subjects(term, (size_t)(stop - term), &subject);
        if (!valid && term < end)
        {
            fprintf(stderr, "generate_attribute_subjects: unknown term '%.*s'\n", (int)(stop - term), term);
        }
        *subjects |= subject;
        at = stop;
    }
    return valid;
}

static int row_order(const void* a, const void* b)
{
    const Row* left = a;
    const Row* right = b;
    const int standard = (int)left->standard - (int)right->standard;
    return standard != 0 ? standard : strcmp(left->spelling, right->spelling);
}

// One ParsedAttrInfo's rows: its spellings (the array `spellings` names) with its predicate's subjects and message. One of
// no spellings (`{}`, an implicit attribute's), which no source writes, has none.
static bool add_rows(const char* text, const char* spellings, size_t spellings_length, uint32_t subjects, bool error, const char* message, size_t message_length)
{
    const bool none = spellings_length == 2 && spellings[0] == '{' && spellings[1] == '}';
    char header[160];
    snprintf(header, sizeof(header), "static constexpr ParsedAttrInfo::Spelling %.*s[] = {", (int)spellings_length, spellings);
    const char* array = none ? NULL : strstr(text, header);
    const char* close = array != NULL ? strstr(array, "};") : NULL;
    bool valid = none || (close != NULL && message_length < sizeof(rows[0].message));
    for (const char* entry = array != NULL ? strstr(array, "{AttributeCommonInfo::AS_") : NULL; valid && entry != NULL && entry < close;
         entry = strstr(entry + 1, "{AttributeCommonInfo::AS_"))
    {
        const char* syntax = entry + strlen("{AttributeCommonInfo::AS_");
        const bool gnu = strncmp(syntax, "GNU,", 4) == 0;
        const bool standard = strncmp(syntax, "C23,", 4) == 0;
        const char* quote = strchr(syntax, '"');
        const char* end = quote != NULL ? strchr(quote + 1, '"') : NULL;
        valid = end != NULL && (size_t)(end - quote - 1) < sizeof(rows[0].spelling) && row_count < ROWS;
        if (valid && (gnu || standard))
        {
            Row* row = &rows[row_count++];
            snprintf(row->spelling, sizeof(row->spelling), "%.*s", (int)(end - quote - 1), quote + 1);
            row->standard = standard;
            row->subjects = subjects;
            row->error = error;
            snprintf(row->message, sizeof(row->message), "%.*s", (int)message_length, message);
        }
    }
    return valid;
}

// Every ParsedAttrInfo of the input with a predicate, at least `minimum` of them: its rows. False where one isn't of the
// one shape or names an unknown term.
static bool read_attributes(const char* text, uint32_t minimum)
{
    static const char decl[] = "bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {";
    bool valid = true;
    uint32_t attributes = 0;
    for (const char* info = strstr(text, "\nstruct ParsedAttrInfo"); valid && info != NULL; info = strstr(info + 1, "\nstruct ParsedAttrInfo"))
    {
        const char* next = strstr(info + 1, "\nstruct ParsedAttrInfo");
        const char* spelling_field = strstr(info, "/*Spellings=*/");
        const char* body = strstr(info, decl);
        if (body != NULL && (next == NULL || body < next) && spelling_field != NULL && (next == NULL || spelling_field < next))
        {
            const char* spellings = spelling_field + strlen("/*Spellings=*/");
            size_t spellings_length = 0;
            while (spellings[spellings_length] != ',' && spellings[spellings_length] != 0)
            {
                spellings_length += 1;
            }
            const char* condition = strstr(body, "if (");
            const char* brace = condition != NULL ? strstr(condition, ") {\n") : NULL;
            const char* diagnostic = brace != NULL ? strstr(brace, "diag::") : NULL;
            const char* message = diagnostic != NULL ? strstr(diagnostic, "<< \"") : NULL;
            const char* message_end = message != NULL ? strchr(message + 4, '"') : NULL;
            valid = message_end != NULL && (next == NULL || message_end < next);
            uint32_t subjects = 0;
            valid = valid && predicate_subjects(condition + 4, brace, &subjects);
            const bool error = valid && strncmp(diagnostic, "diag::err_attribute_wrong_decl_type_str)", 40) == 0;
            valid = valid && (error || strncmp(diagnostic, "diag::warn_attribute_wrong_decl_type_str)", 41) == 0);
            valid = valid && add_rows(text, spellings, spellings_length, subjects, error, message + 4, (size_t)(message_end - message - 4));
            attributes += valid ? 1u : 0u;
            if (!valid)
            {
                fprintf(stderr, "generate_attribute_subjects: an attribute of no known shape: %.*s\n", (int)spellings_length, spellings);
            }
        }
    }
    valid = valid && attributes >= minimum;
    if (valid)
    {
        qsort(rows, row_count, sizeof(rows[0]), row_order);
    }
    for (uint32_t r = 1; valid && r < row_count; r += 1)
    {
        // Two attributes of one spelling of one syntax would make a lookup ambiguous.
        valid = rows[r].standard != rows[r - 1].standard || strcmp(rows[r].spelling, rows[r - 1].spelling) != 0;
        if (!valid)
        {
            fprintf(stderr, "generate_attribute_subjects: '%s' is two attributes' spelling\n", rows[r].spelling);
        }
    }
    return valid;
}

// The value of one atom of an existsInTarget condition (`length` bytes, without spaces around it): a predicate of the
// table, another architecture's comparison false; false in *known where it is none of those.
static bool target_atom(const char* atom, size_t length, bool* known)
{
    static const char arch[] = "T.getArch() == llvm::Triple::";
    bool value = false;
    bool found = false;
    for (size_t p = 0; !found && p < sizeof(predicates) / sizeof(predicates[0]); p += 1)
    {
        found = strlen(predicates[p].text) == length && memcmp(predicates[p].text, atom, length) == 0;
        value = found ? predicates[p].value : value;
    }
    found = found || (length > sizeof(arch) - 1 && memcmp(atom, arch, sizeof(arch) - 1) == 0);
    *known &= found;
    return value;
}

// existsInTarget's `return CONDITION;` for x86-64 Linux: conjuncts (`&&` at the top level) of disjunctions (`||`) of
// atoms, each conjunct in a pair of parentheses or not. False in *known where an atom is unknown.
static bool target_condition(const char* begin, const char* end, bool* known)
{
    bool all = true;
    const char* at = begin;
    while (at < end)
    {
        // One conjunct: to the next `&&` at the top level.
        uint32_t depth = 0;
        const char* stop = at;
        while (stop < end && !(depth == 0 && stop + 1 < end && stop[0] == '&' && stop[1] == '&'))
        {
            depth += *stop == '(' ? 1u : 0u;
            depth -= *stop == ')' && depth != 0 ? 1u : 0u;
            stop += 1;
        }
        const char* first = at;
        const char* last = stop;
        while (first < last && *first == ' ')
        {
            first += 1;
        }
        while (last > first && last[-1] == ' ')
        {
            last -= 1;
        }
        // Its outer parentheses, where the first closes at its end.
        uint32_t level = 0;
        const char* closing = NULL;
        for (const char* c = first; closing == NULL && c < last; c += 1)
        {
            level += *c == '(' ? 1u : 0u;
            level -= *c == ')' ? 1u : 0u;
            closing = level == 0 && *c == ')' ? c : NULL;
        }
        const bool outer = first < last && *first == '(' && closing == last - 1;
        first += outer ? 1 : 0;
        last -= outer ? 1 : 0;
        bool any = false;
        const char* atom = first;
        while (atom < last)
        {
            const char* bar = atom;
            while (bar < last && !(bar + 1 < last && bar[0] == '|' && bar[1] == '|'))
            {
                bar += 1;
            }
            const char* a = atom;
            const char* b = bar;
            while (a < b && *a == ' ')
            {
                a += 1;
            }
            while (b > a && b[-1] == ' ')
            {
                b -= 1;
            }
            any |= target_atom(a, (size_t)(b - a), known);
            atom = bar < last ? bar + 2 : last;
        }
        all &= any;
        at = stop < end ? stop + 2 : end;
    }
    return all;
}

// Whether the attribute of ParsedAttrInfo text [info, next) exists for x86-64 Linux: every one without existsInTarget, and
// one whose condition holds. False in *known where its condition is of no known shape.
static bool target_exists(const char* info, const char* next, bool* known)
{
    static const char method[] = "bool existsInTarget(const TargetInfo &Target) const override {";
    const char* body = strstr(info, method);
    bool exists = true;
    if (body != NULL && (next == NULL || body < next))
    {
        const char* ret = strstr(body, "return ");
        const char* semicolon = ret != NULL ? strchr(ret, ';') : NULL;
        *known &= semicolon != NULL;
        exists = semicolon != NULL && target_condition(ret + strlen("return "), semicolon, known);
    }
    return exists;
}

// The decimal field `/*NAME=*/N` of [info, next), or UINT32_MAX where there is none.
static uint32_t info_field(const char* info, const char* next, const char* name)
{
    char key[64];
    snprintf(key, sizeof(key), "/*%s=*/", name);
    const char* field = strstr(info, key);
    return field != NULL && (next == NULL || field < next) ? (uint32_t)strtoul(field + strlen(key), NULL, 10) : UINT32_MAX;
}

static int fact_order(const void* a, const void* b)
{
    const Fact* left = a;
    const Fact* right = b;
    const int standard = (int)left->standard - (int)right->standard;
    return standard != 0 ? standard : strcmp(left->spelling, right->spelling);
}

// The facts of one spelling (GNU's or C23's) of the array `spellings` of `text`.
static bool add_facts(const char* text, const char* spellings, size_t spellings_length, uint32_t flags, uint32_t minimum, uint32_t maximum)
{
    const bool none = spellings_length == 2 && spellings[0] == '{' && spellings[1] == '}';
    char header[160];
    snprintf(header, sizeof(header), "static constexpr ParsedAttrInfo::Spelling %.*s[] = {", (int)spellings_length, spellings);
    const char* array = none ? NULL : strstr(text, header);
    const char* close = array != NULL ? strstr(array, "};") : NULL;
    bool valid = none || close != NULL;
    for (const char* entry = array != NULL ? strstr(array, "{AttributeCommonInfo::AS_") : NULL; valid && entry != NULL && entry < close;
         entry = strstr(entry + 1, "{AttributeCommonInfo::AS_"))
    {
        const char* syntax = entry + strlen("{AttributeCommonInfo::AS_");
        const bool gnu = strncmp(syntax, "GNU,", 4) == 0;
        const bool standard = strncmp(syntax, "C23,", 4) == 0;
        const char* quote = strchr(syntax, '"');
        const char* end = quote != NULL ? strchr(quote + 1, '"') : NULL;
        valid = end != NULL && (size_t)(end - quote - 1) < sizeof(facts[0].spelling) && fact_count < FACTS;
        if (valid && (gnu || standard))
        {
            Fact* fact = &facts[fact_count++];
            snprintf(fact->spelling, sizeof(fact->spelling), "%.*s", (int)(end - quote - 1), quote + 1);
            fact->standard = standard;
            fact->flags = flags;
            fact->minimum = minimum;
            fact->maximum = maximum;
        }
    }
    return valid;
}

// Every ParsedAttrInfo of `text` that exists for x86-64 Linux, at least `minimum` of them: its spellings' facts, and the
// GNU spellings `kinds` (clang-tblgen --gen-clang-attr-parsed-attr-kinds) makes an IgnoredAttribute. False where a field,
// a language condition or a target condition is of no known shape, or a spelling is two attributes'.
static bool read_facts(const char* text, const char* kinds, uint32_t minimum)
{
    static const char langs[] = "bool acceptsLangOpts(const LangOptions &LangOpts) const override {";
    bool valid = true;
    uint32_t attributes = 0;
    fact_count = 0;
    for (const char* info = strstr(text, "\nstruct ParsedAttrInfo"); valid && info != NULL; info = strstr(info + 1, "\nstruct ParsedAttrInfo"))
    {
        const char* next = strstr(info + 1, "\nstruct ParsedAttrInfo");
        const char* spelling_field = strstr(info, "/*Spellings=*/");
        bool known = true;
        const bool exists = target_exists(info, next, &known);
        const uint32_t arguments = info_field(info, next, "NumArgs");
        const uint32_t optional = info_field(info, next, "OptArgs");
        const uint32_t custom = info_field(info, next, "HasCustomParsing");
        const uint32_t type = info_field(info, next, "IsType");
        const uint32_t statement = info_field(info, next, "IsStmt");
        const uint32_t gcc = info_field(info, next, "IsKnownToGCC");
        const char* lang = strstr(info, langs);
        lang = lang != NULL && (next == NULL || lang < next) ? lang : NULL;
        const char* ret = lang != NULL ? strstr(lang, "return ") : NULL;
        const char* semicolon = ret != NULL ? strchr(ret, ';') : NULL;
        // In C, of clang's language conditions, `(!LangOpts.CPlusPlus)` alone holds; any other names a language C isn't.
        const bool c = lang == NULL || (semicolon != NULL && strncmp(ret, "return (!LangOpts.CPlusPlus);", 30) == 0);
        const bool other = lang != NULL && semicolon != NULL && (strncmp(ret + strlen("return "), "LangOpts.", 9) == 0 || ret[7] == '(');
        const char* handler = strstr(info, "AttrHandling handleDeclAttribute(");
        const bool simple = handler != NULL && (next == NULL || handler < next);
        valid = known && spelling_field != NULL && (next == NULL || spelling_field < next) && arguments != UINT32_MAX && optional != UINT32_MAX &&
                custom != UINT32_MAX && type != UINT32_MAX && statement != UINT32_MAX && gcc != UINT32_MAX && (c || other);
        if (!valid)
        {
            fprintf(stderr, "generate_attribute_subjects: an attribute's facts of no known shape: %.40s\n", info + 1);
        }
        if (valid && exists)
        {
            const char* spellings = spelling_field + strlen("/*Spellings=*/");
            size_t spellings_length = 0;
            while (spellings[spellings_length] != ',' && spellings[spellings_length] != 0)
            {
                spellings_length += 1;
            }
            const uint32_t flags = (gcc != 0 ? FACT_GCC : 0) | (c ? 0 : FACT_NOT_C) | (simple ? FACT_SIMPLE : 0) | (type != 0 ? FACT_TYPE : 0) |
                                   (statement != 0 ? FACT_STATEMENT : 0) | (custom != 0 ? FACT_CUSTOM_PARSING : 0) | (optional == 15 ? FACT_VARIADIC : 0);
            valid = add_facts(text, spellings, spellings_length, flags, arguments, optional == 15 ? UINT32_MAX : arguments + optional);
            attributes += 1;
        }
    }
    // The GNU section of the kinds' matcher, to its first other syntax's: each `IgnoredAttribute;\t // "name"`.
    const char* gnu = kinds != NULL ? strstr(kinds, "if (AttributeCommonInfo::AS_GNU == Syntax) {") : NULL;
    const char* gnu_end = gnu != NULL ? strstr(gnu + 1, "if (AttributeCommonInfo::AS_") : NULL;
    for (const char* ignored = gnu != NULL ? strstr(gnu, "AttributeCommonInfo::IgnoredAttribute;") : NULL; valid && ignored != NULL && (gnu_end == NULL || ignored < gnu_end);
         ignored = strstr(ignored + 1, "AttributeCommonInfo::IgnoredAttribute;"))
    {
        const char* quote = strchr(ignored, '"');
        const char* end = quote != NULL ? strchr(quote + 1, '"') : NULL;
        valid = end != NULL && (size_t)(end - quote - 1) < sizeof(facts[0].spelling) && fact_count < FACTS;
        if (valid)
        {
            Fact* fact = &facts[fact_count++];
            snprintf(fact->spelling, sizeof(fact->spelling), "%.*s", (int)(end - quote - 1), quote + 1);
            fact->standard = false;
            fact->flags = FACT_IGNORED;
            fact->minimum = 0;
            fact->maximum = UINT32_MAX;
        }
    }
    valid = valid && attributes >= minimum && (kinds == NULL || gnu != NULL);
    if (valid)
    {
        qsort(facts, fact_count, sizeof(facts[0]), fact_order);
    }
    for (uint32_t f = 1; valid && f < fact_count; f += 1)
    {
        valid = facts[f].standard != facts[f - 1].standard || strcmp(facts[f].spelling, facts[f - 1].spelling) != 0;
        if (!valid)
        {
            fprintf(stderr, "generate_attribute_subjects: '%s' is two attributes' spelling for x86-64\n", facts[f].spelling);
        }
    }
    return valid;
}

static bool write_table(FILE* output, const char* source)
{
    bool valid = fprintf(output,
                         "// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n"
                         "// Generated by tools/generate_attribute_subjects.c; do not edit. Derived LLVM data, part of the LLVM Project, under the\n"
                         "// Apache License v2.0 with LLVM Exceptions (LICENSES/LLVM.txt): the subjects of clang's attributes in C, from\n"
                         "// clang-tblgen --gen-clang-attr-parsed-attr-impl (and --gen-clang-attr-parsed-attr-kinds for its ignored spellings) of\n"
                         "// %s; see THIRD_PARTY_NOTICES.md.\n"
                         "// BUSTER_CHECK_SUBJECT_GNU and BUSTER_CHECK_SUBJECT_C23 rows: a spelling, the C declarations it applies to\n"
                         "// (BUSTER_CHECK_SUBJECT_*, c_check_attribute.c), whether another is clang's error rather than its warning, and the\n"
                         "// subjects clang's message names. Each list in its spellings' byte order.\n",
                         source) > 0;
    for (uint32_t r = 0; valid && r < row_count; r += 1)
    {
        valid = fprintf(output, "BUSTER_CHECK_SUBJECT_%s(\"%s\", 0x%xu, %s, \"%s\")\n", rows[r].standard ? "C23" : "GNU", rows[r].spelling, rows[r].subjects,
                        rows[r].error ? "true" : "false", rows[r].message) > 0;
    }
    valid = valid && (fact_count == 0 || fprintf(output,
                                                 "// BUSTER_CHECK_FACT_GNU and BUSTER_CHECK_FACT_C23 rows: each spelling of an attribute clang has for x86-64 Linux,\n"
                                                 "// its facts (BUSTER_CHECK_FACT_*, c_check_attribute.c: GCC knows it, it isn't C's, clang's generated handler\n"
                                                 "// applies it, it is a type's or a statement's, its arguments are its parsing's, its last one is variadic, clang\n"
                                                 "// ignores it everywhere) and its least and most arguments (UINT32_MAX for any). Each list in byte order.\n") > 0);
    for (uint32_t f = 0; valid && f < fact_count; f += 1)
    {
        valid = fprintf(output, "BUSTER_CHECK_FACT_%s(\"%s\", 0x%xu, %uu, %uu)\n", facts[f].standard ? "C23" : "GNU", facts[f].spelling, facts[f].flags,
                        facts[f].minimum, facts[f].maximum) > 0;
    }
    valid = valid && fflush(output) == 0 && !ferror(output);
    return valid;
}

// The generator against a synthetic input: an attribute's GNU and C23 spellings with its subjects and message, an
// unknown term refused, a spelling of two attributes refused, and a write that can't complete refused.
static bool self_test(void)
{
    static const char good[] =
        "static constexpr ParsedAttrInfo::Spelling ColdSpellings[] = {\n"
        "  {AttributeCommonInfo::AS_GNU, \"cold\"},\n"
        "  {AttributeCommonInfo::AS_CXX11, \"gnu::cold\"},\n"
        "  {AttributeCommonInfo::AS_C23, \"gnu::cold\"},\n"
        "};\n"
        "\nstruct ParsedAttrInfoCold final : public ParsedAttrInfo {\n"
        "    /*Spellings=*/ColdSpellings,\n"
        "bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {\n"
        "  if (!isa<FunctionDecl>(D) && !isa<ObjCMethodDecl>(D)) {\n"
        "    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_decl_type_str)\n"
        "      << Attr << Attr.isRegularKeywordAttribute() << \"functions\";\n"
        "    return false;\n"
        "  }\n"
        "  return true;\n"
        "}\n";
    row_count = 0;
    bool valid = read_attributes(good, 1) && row_count == 2 && !rows[0].standard && strcmp(rows[0].spelling, "cold") == 0 && rows[1].standard &&
                 strcmp(rows[1].spelling, "gnu::cold") == 0 && rows[0].subjects == SUBJECT_FUNCTION && rows[0].error && strcmp(rows[1].message, "functions") == 0;
    static const char twice[] =
        "static constexpr ParsedAttrInfo::Spelling ASpellings[] = {\n  {AttributeCommonInfo::AS_GNU, \"same\"},\n};\n"
        "\nstruct ParsedAttrInfoA final : public ParsedAttrInfo {\n    /*Spellings=*/ASpellings,\n"
        "bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {\n  if (!isa<VarDecl>(D)) {\n"
        "    S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)\n      << Attr << Attr.isRegularKeywordAttribute() << \"variables\";\n"
        "\nstruct ParsedAttrInfoB final : public ParsedAttrInfo {\n    /*Spellings=*/ASpellings,\n"
        "bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {\n  if (!isa<VarDecl>(D)) {\n"
        "    S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)\n      << Attr << Attr.isRegularKeywordAttribute() << \"variables\";\n";
    row_count = 0;
    valid = valid && !read_attributes(twice, 1);
    static const char unknown[] =
        "\nstruct ParsedAttrInfoX final : public ParsedAttrInfo {\n"
        "    /*Spellings=*/XSpellings,\n"
        "bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {\n"
        "  if (!isMystery(D)) {\n"
        "    S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)\n"
        "      << Attr << Attr.isRegularKeywordAttribute() << \"mysteries\";\n";
    row_count = 0;
    valid = valid && !read_attributes(unknown, 1);
    uint32_t subjects = 0;
    static const char predicate[] = "!isa<FunctionDecl>(D) && !isGlobalVar(D) && !isa<ObjCMethodDecl>(D)";
    valid = valid && predicate_subjects(predicate, predicate + sizeof(predicate) - 1, &subjects) && subjects == (SUBJECT_FUNCTION | SUBJECT_GLOBAL);
    FILE* full = fopen("/dev/full", "w");
    row_count = 1;
    snprintf(rows[0].spelling, sizeof(rows[0].spelling), "cold");
    snprintf(rows[0].message, sizeof(rows[0].message), "functions");
    valid = valid && full != NULL && !write_table(full, "a test");
    row_count = 0;
    if (full != NULL)
    {
        fclose(full);
    }
    return valid;
}

int main(int argc, char** argv)
{
    int status = 1;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        const bool passed = self_test();
        fprintf(stderr, "generate_attribute_subjects: self-test %s\n", passed ? "passed" : "failed");
        status = passed ? 0 : 1;
    }
    else if (argc == 4)
    {
        size_t length = 0;
        size_t kinds_length = 0;
        char* text = read_all(argv[1], &length);
        char* kinds = read_all(argv[2], &kinds_length);
        const bool valid = text != NULL && length != 0 && kinds != NULL && kinds_length != 0 && read_attributes(text, 100) && read_facts(text, kinds, 300) &&
                           write_table(stdout, argv[3]);
        if (!valid)
        {
            fprintf(stderr, "generate_attribute_subjects: %s: failed\n", argv[1]);
        }
        free(text);
        free(kinds);
        status = valid ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: %s --self-test | %s PARSED_ATTR_IMPL.inc PARSED_ATTR_KINDS.inc \"SOURCE DESCRIPTION\"\n", argv[0], argv[0]);
    }
    return status;
}
