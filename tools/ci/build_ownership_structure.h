// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// A lexical invariant for shared fixture roots. Private unit islands do not contribute; shared declaration headers,
// includes and macro names do. Function bodies and initializers remain reviewable without a duplicate manifest.
#include <ctype.h>

typedef struct OwnershipCondition OwnershipCondition;
struct OwnershipCondition
{
    uint64_t yes;
    uint64_t no;
};

typedef struct OwnershipStructure OwnershipStructure;
struct OwnershipStructure
{
    uint64_t hash;
    uint32_t declarations;
    bool valid;
};

static uint64_t ownership_hash_bytes(uint64_t hash, const char* bytes, size_t length)
{
    for (size_t i = 0; i < length; i += 1)
    {
        hash = (hash ^ (unsigned char)bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t ownership_selector(const OwnershipUnits* units, const char* name, size_t length, bool* valid)
{
    uint64_t mask = 0;
    for (uint32_t unit = 0; unit < units->count; unit += 1)
    {
        char selector[256] = "BUSTER_UNIT_";
        const char* source = units->names[unit];
        if (strcmp(source, "main.c") == 0)
        {
            strcat(selector, "DRIVER");
        }
        else
        {
            source += strlen("main_unit_");
            size_t at = strlen(selector);
            for (size_t i = 0; source[i] != '.' && source[i] != 0 && at + 1 < sizeof(selector); i += 1)
            {
                selector[at++] = (char)toupper((unsigned char)source[i]);
            }
            selector[at] = 0;
        }
        if (strlen(selector) == length && memcmp(selector, name, length) == 0)
        {
            mask |= UINT64_C(1) << unit;
        }
    }
    *valid = *valid && mask != 0;
    return mask;
}

static OwnershipCondition ownership_condition_pair(uint64_t yes, uint64_t no)
{
    const OwnershipCondition result = {
        .yes = yes,
        .no = no,
    };
    return result;
}

static void ownership_condition_apply(char operation, OwnershipCondition* values, uint32_t* count, bool* valid)
{
    if (*count == 0 || (operation != '!' && *count < 2))
    {
        *valid = false;
    }
    else if (operation == '!')
    {
        const OwnershipCondition value = values[*count - 1];
        values[*count - 1] = ownership_condition_pair(value.no, value.yes);
    }
    else
    {
        const OwnershipCondition right = values[--*count];
        OwnershipCondition* left = &values[*count - 1];
        if (operation == '&')
        {
            left->yes &= right.yes;
            left->no |= right.no;
        }
        else
        {
            left->yes |= right.yes;
            left->no &= right.no;
        }
    }
}

static uint32_t ownership_condition_precedence(char operation)
{
    return operation == '!' ? 3 : operation == '&' ? 2 : operation == '|' ? 1 : 0;
}

static const char* ownership_directive_space(const char* p, bool* valid)
{
    bool more = true;
    while (more && *valid)
    {
        more = false;
        while (*p == ' ' || *p == '\t' || *p == '\r' || (*p == '\\' && p[1] == '\n'))
        {
            p += *p == '\\' ? 2 : 1;
        }
        if (p[0] == '/' && p[1] == '*')
        {
            const char* end = strstr(p + 2, "*/");
            *valid = end != 0;
            p = end != 0 ? end + 2 : p + strlen(p);
            more = true;
        }
    }
    return p;
}

// Unknown architecture/configuration predicates permit both branches. Unit predicates are exact, including the
// standalone fallback, which is false within a unity unit. This keeps the invariant independent of host ISA.
static OwnershipCondition ownership_condition(const OwnershipUnits* units, const char* expression, bool* selectors_valid)
{
    const uint64_t all = ownership_all(units);
    OwnershipCondition values[128];
    char operations[128];
    uint32_t value_count = 0;
    uint32_t operation_count = 0;
    bool valid = true;
    bool want_value = true;
    const char* p = expression;
    while (*p != 0 && valid)
    {
        while (isspace((unsigned char)*p))
        {
            p += 1;
        }
        p = ownership_directive_space(p, &valid);
        if (*p == 0 || (p[0] == '/' && p[1] == '/'))
        {
            break;
        }
        if (operation_count == 128 || value_count == 128)
        {
            valid = false;
        }
        else if (want_value && (*p == '!' || *p == '('))
        {
            operations[operation_count++] = *p++;
        }
        else if (!want_value && *p == ')')
        {
            while (operation_count != 0 && operations[operation_count - 1] != '(' && valid)
            {
                ownership_condition_apply(operations[--operation_count], values, &value_count, &valid);
            }
            valid = valid && operation_count != 0;
            operation_count -= operation_count != 0;
            p += 1;
        }
        else if (!want_value && ((p[0] == '&' && p[1] == '&') || (p[0] == '|' && p[1] == '|')))
        {
            const char operation = p[0];
            while (operation_count != 0 && operations[operation_count - 1] != '(' &&
                   ownership_condition_precedence(operations[operation_count - 1]) >= ownership_condition_precedence(operation) && valid)
            {
                ownership_condition_apply(operations[--operation_count], values, &value_count, &valid);
            }
            operations[operation_count++] = operation;
            want_value = true;
            p += 2;
        }
        else if (want_value && (isalnum((unsigned char)*p) || *p == '_'))
        {
            const char* word = p;
            while (isalnum((unsigned char)*p) || *p == '_')
            {
                p += 1;
            }
            const size_t length = (size_t)(p - word);
            while (isspace((unsigned char)*p))
            {
                p += 1;
            }
            OwnershipCondition value = ownership_condition_pair(all, all);
            if (length == 7 && memcmp(word, "defined", 7) == 0)
            {
                const bool parentheses = *p == '(';
                p += parentheses;
                while (isspace((unsigned char)*p))
                {
                    p += 1;
                }
                const char* macro = p;
                while (isalnum((unsigned char)*p) || *p == '_')
                {
                    p += 1;
                }
                if ((size_t)(p - macro) == 11 && memcmp(macro, "BUSTER_UNIT", 11) == 0)
                {
                    value.no = 0;
                }
                while (isspace((unsigned char)*p))
                {
                    p += 1;
                }
                if (parentheses)
                {
                    valid = *p == ')';
                    p += *p == ')';
                }
            }
            else if (length == 14 && memcmp(word, "BUSTER_IN_UNIT", 14) == 0 && *p == '(')
            {
                p += 1;
                while (isspace((unsigned char)*p))
                {
                    p += 1;
                }
                const char* selector = p;
                while (isalnum((unsigned char)*p) || *p == '_')
                {
                    p += 1;
                }
                value.yes = ownership_selector(units, selector, (size_t)(p - selector), selectors_valid);
                value.no = all & ~value.yes;
                while (isspace((unsigned char)*p))
                {
                    p += 1;
                }
                valid = *p == ')';
                p += *p == ')';
            }
            else if (length == 11 && memcmp(word, "BUSTER_UNIT", 11) == 0 &&
                     ((p[0] == '=' && p[1] == '=') || (p[0] == '!' && p[1] == '=')))
            {
                const bool unequal = *p == '!';
                p += 2;
                while (isspace((unsigned char)*p))
                {
                    p += 1;
                }
                const char* selector = p;
                while (isalnum((unsigned char)*p) || *p == '_')
                {
                    p += 1;
                }
                value.yes = ownership_selector(units, selector, (size_t)(p - selector), selectors_valid);
                value.no = all & ~value.yes;
                if (unequal)
                {
                    value = ownership_condition_pair(value.no, value.yes);
                }
            }
            else if (length == 1 && (*word == '0' || *word == '1'))
            {
                value = ownership_condition_pair(*word == '1' ? all : 0, *word == '0' ? all : 0);
            }
            else if (*p == '(')
            {
                uint32_t depth = 0;
                do
                {
                    depth += *p == '(';
                    depth -= *p == ')';
                    p += 1;
                } while (*p != 0 && depth != 0);
                valid = depth == 0;
            }
            values[value_count++] = value;
            want_value = false;
        }
        else
        {
            valid = false;
        }
    }
    while (operation_count != 0 && valid)
    {
        const char operation = operations[--operation_count];
        valid = operation != '(';
        if (valid)
        {
            ownership_condition_apply(operation, values, &value_count, &valid);
        }
    }
    return valid && !want_value && value_count == 1 ? values[0] : ownership_condition_pair(all, all);
}

static void ownership_structure_entry(OwnershipStructure* result, uint64_t owners, uint64_t header)
{
    if ((owners & (owners - 1)) != 0)
    {
        result->hash = ownership_hash_bytes(result->hash, (const char*)&owners, sizeof(owners));
        result->hash = ownership_hash_bytes(result->hash, (const char*)&header, sizeof(header));
        result->declarations += 1;
    }
}

// Keep only shared top-level work in the fingerprint. Track raw tokens without expanding macros or reading system
// headers. The C style's complete declarations, balanced bodies and one declarator per statement keep this small.
static OwnershipStructure ownership_structure(const OwnershipUnits* units, uint64_t owners, const char* text)
{
    static const uint64_t seed = UINT64_C(14695981039346656037);
    OwnershipStructure result = {
        .hash = seed,
        .valid = true,
    };
    struct
    {
        uint64_t parent;
        uint64_t remaining;
    } stack[128];
    uint32_t conditions = 0;
    uint32_t braces = 0;
    uint32_t parentheses = 0;
    bool declaration = false;
    bool header = false;
    bool function = false;
    bool parameters = false;
    bool aggregate = false;
    bool initialized = false;
    uint64_t declared_owners = 0;
    uint64_t hash = seed;
    const char* p = text;
    while (*p != 0 && result.valid)
    {
        if (isspace((unsigned char)*p))
        {
            p += 1;
        }
        else if (p[0] == '/' && p[1] == '/')
        {
            p += strcspn(p, "\n");
        }
        else if (p[0] == '/' && p[1] == '*')
        {
            const char* end = strstr(p + 2, "*/");
            result.valid = end != 0;
            p = end != 0 ? end + 2 : p + strlen(p);
        }
        else if (*p == '#')
        {
            char directive[8192] = { 0 };
            size_t length = 0;
            bool continued = false;
            do
            {
                const size_t bytes = strcspn(p, "\n");
                continued = bytes != 0 && p[bytes - 1] == '\\';
                const size_t keep = bytes - continued;
                if (keep + length + 2 >= sizeof(directive))
                {
                    result.valid = false;
                }
                else
                {
                    memcpy(directive + length, p, keep);
                    length += keep;
                    directive[length++] = ' ';
                    directive[length] = 0;
                }
                p += bytes + (p[bytes] != 0);
            } while (continued && result.valid);
            const char* word = ownership_directive_space(directive + 1, &result.valid);
            size_t name_length = 0;
            while (isalnum((unsigned char)word[name_length]) || word[name_length] == '_')
            {
                name_length += 1;
            }
            const char* arguments = ownership_directive_space(word + name_length, &result.valid);
            if ((name_length == 2 && memcmp(word, "if", 2) == 0) ||
                (name_length == 5 && memcmp(word, "ifdef", 5) == 0) || (name_length == 6 && memcmp(word, "ifndef", 6) == 0))
            {
                OwnershipCondition value = ownership_condition_pair(owners, owners);
                if (name_length == 2)
                {
                    value = ownership_condition(units, arguments, &result.valid);
                }
                else if (strncmp(arguments, "BUSTER_UNIT", 11) == 0 && !isalnum((unsigned char)arguments[11]) && arguments[11] != '_')
                {
                    value = ownership_condition_pair(name_length == 5 ? owners : 0, name_length == 6 ? owners : 0);
                }
                if (conditions == 128)
                {
                    result.valid = false;
                }
                else
                {
                    stack[conditions].parent = owners;
                    stack[conditions++].remaining = owners & value.no;
                    owners &= value.yes;
                }
            }
            else if (name_length == 4 && memcmp(word, "elif", 4) == 0)
            {
                const OwnershipCondition value = ownership_condition(units, arguments, &result.valid);
                result.valid = result.valid && conditions != 0;
                if (conditions != 0)
                {
                    owners = stack[conditions - 1].remaining & value.yes;
                    stack[conditions - 1].remaining &= value.no;
                }
            }
            else if (name_length == 4 && memcmp(word, "else", 4) == 0)
            {
                result.valid = conditions != 0;
                if (conditions != 0)
                {
                    owners = stack[conditions - 1].remaining;
                    stack[conditions - 1].remaining = 0;
                }
            }
            else if (name_length == 5 && memcmp(word, "endif", 5) == 0)
            {
                result.valid = conditions != 0;
                if (conditions != 0)
                {
                    owners = stack[--conditions].parent;
                }
            }
            else if ((braces == 0 && name_length == 6 && memcmp(word, "define", 6) == 0) ||
                     (name_length == 7 && memcmp(word, "include", 7) == 0))
            {
                const bool quoted = name_length == 7 && *arguments == '"';
                const char* quote = quoted ? strchr(arguments + 1, '"') : 0;
                result.valid = result.valid && (!quoted || quote != 0);
                const size_t bytes = name_length == 6 ? strcspn(arguments, " (\t\r\n") :
                                     quoted ? (quote != 0 ? (size_t)(quote - arguments) + 1 : 0) : strcspn(arguments, " \t\r\n");
                uint64_t entry = ownership_hash_bytes(seed, word, name_length);
                entry = ownership_hash_bytes(entry, arguments, bytes);
                ownership_structure_entry(&result, owners, entry);
            }
        }
        else
        {
            const char* token = p;
            const bool literal = *p == '"' || *p == '\'';
            if (literal)
            {
                const char quote = *p++;
                while (*p != 0 && *p != quote)
                {
                    p += *p == '\\' && p[1] != 0;
                    p += 1;
                }
                result.valid = *p == quote;
                p += *p == quote;
            }
            else if (isalnum((unsigned char)*p) || *p == '_')
            {
                do
                {
                    p += 1;
                } while (isalnum((unsigned char)*p) || *p == '_');
            }
            else
            {
                p += 1;
            }
            if (!declaration && braces == 0)
            {
                declaration = true;
                header = true;
                hash = seed;
                declared_owners = owners;
                parameters = false;
                initialized = false;
                aggregate = ((size_t)(p - token) == 7 && memcmp(token, "typedef", 7) == 0) ||
                            ((size_t)(p - token) == 6 && memcmp(token, "struct", 6) == 0) ||
                            ((size_t)(p - token) == 5 && memcmp(token, "union", 5) == 0) ||
                            ((size_t)(p - token) == 4 && memcmp(token, "enum", 4) == 0);
            }
            if (header)
            {
                hash = ownership_hash_bytes(hash, literal ? "<literal>" : token, literal ? 9 : (size_t)(p - token));
                hash = ownership_hash_bytes(hash, "\0", 1);
            }
            if (!literal && p - token == 1)
            {
                const char character = *token;
                if (character == '(')
                {
                    parentheses += 1;
                    parameters = true;
                }
                else if (character == ')')
                {
                    result.valid = parentheses != 0;
                    parentheses -= parentheses != 0;
                }
                else if (character == '=' && braces == 0 && parentheses == 0)
                {
                    initialized = true;
                    header = false;
                }
                else if (character == '{')
                {
                    if (braces == 0)
                    {
                        function = parameters && !initialized && !aggregate;
                        header = aggregate && !initialized;
                    }
                    braces += 1;
                }
                else if (character == '}')
                {
                    result.valid = braces != 0;
                    braces -= braces != 0;
                    if (braces == 0 && function)
                    {
                        ownership_structure_entry(&result, declared_owners, hash);
                        declaration = false;
                        function = false;
                    }
                    else if (braces == 0 && !initialized)
                    {
                        header = true;
                    }
                }
                else if (character == ';' && braces == 0 && parentheses == 0)
                {
                    ownership_structure_entry(&result, declared_owners, hash);
                    declaration = false;
                }
            }
        }
    }
    result.valid = result.valid && conditions == 0 && braces == 0 && parentheses == 0 && !declaration;
    return result;
}
