// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The code clang 23 generates for GNU's function multiversioning by target strings on x86-64 Linux (#1838;
// CodeGenModule::emitMultiVersionFunctions, GetOrCreateMultiVersionResolver, getMangledNameImpl's AppendTargetMangling
// and AppendTargetClonesMangling, and CodeGenFunction::EmitX86MultiVersionResolver and FormX86ResolverCondition, at
// llvmorg-23.1.4, read for their rules and written again in C). The checker keeps each version apart
// (c_check_target_versions.c); a function's versions form a group:
//
// - target's versions: each version's body is its own symbol, its mangled name (f for "default"; else f., then arch_CPU,
//   then its features sorted by their priorities, the highest first, joined by '_': f.arch_znver4, f.popcnt_sse4.2),
//   compiled with its own features (check_program_targets). Every reference to a version, a call or an address, reaches
//   the group's reference object R (a synthetic object at the first declaration's attribute name), whose symbol is
//   f.ifunc, an IFUNC (weak, local in a static group) at the resolver f.resolver, which the unit emits where it has a
//   reference.
// - target_clones: the function's own symbol f is the IFUNC (weak, local where it's static) at f.resolver, with f.ifunc
//   an alias of it, which the unit emits where it defines or references the function. Its body is compiled once, with
//   the unit's features, as each version is valid there (clang compiles a copy per version, with its features), at the
//   first version's name (f.avx2.0: the version, arch_CPU for arch=, and its place among the versions that aren't
//   "default", which takes their count) and the others' names, a synthetic object's symbol at the attribute's name.
// - The resolver calls __cpu_indicator_init and returns the first version whose condition libgcc's CPU model meets, the
//   versions sorted as clang's llvm::stable_sort sorts them, by their priorities (getFMVPriority: the highest of their
//   features' priorities, doubled, and an arch='s key feature's, doubled plus one), the highest first, in their
//   declarations' order otherwise. A version's condition is its arch= (__builtin_cpu_is's test of the model's vendor,
//   type or subtype; a microarchitecture level's feature bit) and its features' bits; one of neither, as "default", is
//   taken unconditionally, and a model none matches is a ud2.
//
// A group has a few versions, and each step reads those few records: scalar, as a vector pass would set up more than
// it saves. A unit without a multiversioned function (CheckInput.target_multiversion) never calls these functions.
#ifndef BUSTER_C_CHECK_PROGRAM_TARGET_VERSIONS_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_TARGET_VERSIONS_INCLUDED

// The most versions a group's resolver takes, and the most features a version names.
#define BUSTER_CHECK_PROGRAM_TARGET_OPTIONS 64u
#define BUSTER_CHECK_PROGRAM_TARGET_FEATURES 32u

// A processor's key feature's priority (c_check_target_versions.inc), or -2 for no processor of `name`.
static int32_t check_program_target_arch_priority(String8 name)
{
    static const struct
    {
        const char* name;
        int32_t priority;
    } rows[] =
    {
#define BUSTER_CHECK_TARGET_VERSION_FEATURE(NAME, PRIORITY, WORD, BIT)
#define BUSTER_CHECK_TARGET_VERSION_LEVEL(NAME, WORD, BIT)
#define BUSTER_CHECK_TARGET_VERSION_ARCH(NAME, PRIORITY) { NAME, PRIORITY },
#include "c_check_target_versions.inc"
#undef BUSTER_CHECK_TARGET_VERSION_FEATURE
#undef BUSTER_CHECK_TARGET_VERSION_LEVEL
#undef BUSTER_CHECK_TARGET_VERSION_ARCH
    };
    int32_t result = -2;
    for (uint32_t r = 0; result == -2 && r < BUSTER_ARRAY_LENGTH(rows); r += 1)
    {
        result = strlen(rows[r].name) == name.length && memcmp(rows[r].name, name.pointer, name.length) == 0 ? rows[r].priority : -2;
    }
    return result;
}

// A microarchitecture level's bit of libgcc's CPU model (`*word`, `*bit`), where `name` names one.
static bool check_program_target_level(String8 name, uint32_t* word, uint32_t* bit)
{
    static const struct
    {
        const char* name;
        uint32_t word;
        uint32_t bit;
    } rows[] =
    {
#define BUSTER_CHECK_TARGET_VERSION_FEATURE(NAME, PRIORITY, WORD, BIT)
#define BUSTER_CHECK_TARGET_VERSION_LEVEL(NAME, WORD, BIT) { NAME, WORD, BIT },
#define BUSTER_CHECK_TARGET_VERSION_ARCH(NAME, PRIORITY)
#include "c_check_target_versions.inc"
#undef BUSTER_CHECK_TARGET_VERSION_FEATURE
#undef BUSTER_CHECK_TARGET_VERSION_LEVEL
#undef BUSTER_CHECK_TARGET_VERSION_ARCH
    };
    bool found = false;
    for (uint32_t r = 0; !found && r < BUSTER_ARRAY_LENGTH(rows); r += 1)
    {
        found = strlen(rows[r].name) == name.length && memcmp(rows[r].name, name.pointer, name.length) == 0;
        *word = found ? rows[r].word : *word;
        *bit = found ? rows[r].bit : *bit;
    }
    return found;
}

// __builtin_cpu_is's test of `name` (c_builtin_cpu.inc): the model's field (0 the vendor, 1 the type, 2 the subtype) and
// its value, where `name` names one.
static bool check_program_target_cpu_is(String8 name, uint32_t* field, uint32_t* value)
{
    static const struct
    {
        const char* name;
        uint32_t field;
        uint32_t value;
    } rows[] =
    {
#define BUSTER_CBUILTIN_CPU_FEATURE(NAME, WORD, BIT)
#define BUSTER_CBUILTIN_CPU_NAME(NAME, FIELD, VALUE) { NAME, FIELD, VALUE },
#include "c_builtin_cpu.inc"
#undef BUSTER_CBUILTIN_CPU_FEATURE
#undef BUSTER_CBUILTIN_CPU_NAME
    };
    bool found = false;
    for (uint32_t r = 0; !found && r < BUSTER_ARRAY_LENGTH(rows); r += 1)
    {
        found = strlen(rows[r].name) == name.length && memcmp(rows[r].name, name.pointer, name.length) == 0;
        *field = found ? rows[r].field : *field;
        *value = found ? rows[r].value : *value;
    }
    return found;
}

// A version as the resolver tests it: its arch= (empty for none) and features (`feature_count`), what its name adds to
// the function's (`suffix`, without the leading period), and whether it is "default". A target_clones version's place
// among the versions that aren't "default" (`index`), and whether an earlier one is the same (`repeated`).
typedef struct CheckProgramTargetOption CheckProgramTargetOption;
struct CheckProgramTargetOption
{
    String8 arch;
    String8 features[BUSTER_CHECK_PROGRAM_TARGET_FEATURES];
    uint32_t feature_count;
    bool is_default;
    bool repeated;
    uint32_t index;
    // The resolver's: the version's symbol, its priority, its condition (the CPU model's mask of four words, and
    // __builtin_cpu_is's field and value where `cpu_is`).
    SymbolId symbol;
    uint32_t priority;
    uint32_t mask[4];
    bool cpu_is;
    uint32_t field;
    uint32_t value;
};

// The option of target's version string `text`: its arch= and features (clang's parseTargetAttr; the checker took each
// feature as a version's). Whether it fits.
static bool check_program_target_parse(String8 text, CheckProgramTargetOption* option)
{
    *option = (CheckProgramTargetOption){ .is_default = check_input_attribute_equal(text, "default") };
    bool fits = true;
    for (uint32_t cursor = 0; !option->is_default && cursor <= text.length;)
    {
        uint32_t end = cursor;
        while (end < text.length && text.pointer[end] != ',')
        {
            end += 1;
        }
        const String8 item = check_target_version_slice(text, cursor, end);
        cursor = end + 1;
        uint32_t equals = 0;
        while (equals < item.length && item.pointer[equals] != '=')
        {
            equals += 1;
        }
        const String8 value = equals < item.length ? check_target_version_slice(item, equals + 1, (uint32_t)item.length) : (String8){ 0 };
        if (check_target_version_prefix(item, "arch="))
        {
            option->arch = option->arch.pointer == NULL ? value : option->arch;
        }
        else if (check_target_version_prefix(item, "fpmath=") || check_target_version_prefix(item, "branch-protection=") ||
                 check_target_version_prefix(item, "tune="))
        {
        }
        else if (option->feature_count < BUSTER_CHECK_PROGRAM_TARGET_FEATURES)
        {
            option->features[option->feature_count++] = item;
        }
        else
        {
            fits = false;
        }
    }
    return fits;
}

// The priority of a feature a version may name, doubled (getFMVPriority's), or 0.
static uint32_t check_program_target_feature_priority(String8 feature)
{
    const int32_t row = check_target_version_feature(feature);
    return row >= 0 ? check_target_version_features[row].priority << 1 : 0;
}

// The resolver's condition and priority of `option`: whether clang's are defined (an arch= of no processor, or of one
// whose key feature has no priority, is undefined behavior in clang's getFMVPriority: refused by name).
static bool check_program_target_condition(CheckProgramTargetOption* option)
{
    bool valid = true;
    option->priority = 0;
    memset(option->mask, 0, sizeof(option->mask));
    option->cpu_is = false;
    for (uint32_t f = 0; f < option->feature_count; f += 1)
    {
        const int32_t row = check_target_version_feature(option->features[f]);
        valid &= row >= 0;
        if (row >= 0)
        {
            const CheckTargetVersionFeature* feature = check_target_version_features + row;
            option->mask[feature->word] |= UINT32_C(1) << feature->bit;
            option->priority = option->priority > feature->priority << 1 ? option->priority : feature->priority << 1;
        }
    }
    if (option->arch.length != 0)
    {
        const int32_t key = check_program_target_arch_priority(option->arch);
        valid &= key >= 0;
        const uint32_t priority = key >= 0 ? ((uint32_t)key << 1) + 1 : 0;
        option->priority = option->priority > priority ? option->priority : priority;
        uint32_t word = 0;
        uint32_t bit = 0;
        const bool level = option->arch.length >= 6 && memcmp(option->arch.pointer, "x86-64", 6) == 0;
        if (level)
        {
            valid &= check_program_target_level(option->arch, &word, &bit);
            option->mask[word] |= UINT32_C(1) << bit;
        }
        else
        {
            option->cpu_is = check_program_target_cpu_is(option->arch, &option->field, &option->value);
            valid &= option->cpu_is;
        }
    }
    return valid;
}

// The name a version's symbol adds to the function's, after a period, written to `text` (room for `capacity` bytes):
// target's (arch_CPU, then the features by their priorities, the highest first, '_' between) or target_clones' (its
// version, arch_CPU for arch=, then a period and its index). Returns its length, 0 where it didn't fit.
static uint32_t check_program_target_suffix(const CheckProgramTargetOption* option, bool clones, char* text, uint32_t capacity)
{
    uint32_t length = 0;
    bool fits = true;
    // The pieces in their order.
    String8 pieces[BUSTER_CHECK_PROGRAM_TARGET_FEATURES + 1];
    uint32_t piece_count = 0;
    if (option->arch.length != 0)
    {
        pieces[piece_count++] = option->arch;
    }
    uint32_t order[BUSTER_CHECK_PROGRAM_TARGET_FEATURES];
    for (uint32_t f = 0; f < option->feature_count; f += 1)
    {
        // Insertion by priority, the highest first.
        const uint32_t priority = check_program_target_feature_priority(option->features[f]);
        uint32_t at = f;
        while (at > 0 && check_program_target_feature_priority(option->features[order[at - 1]]) < priority)
        {
            order[at] = order[at - 1];
            at -= 1;
        }
        order[at] = f;
    }
    for (uint32_t f = 0; f < option->feature_count; f += 1)
    {
        pieces[piece_count++] = option->features[order[f]];
    }
    for (uint32_t p = 0; fits && p < piece_count; p += 1)
    {
        const bool arch = p == 0 && option->arch.length != 0;
        const uint64_t needed = (p != 0 && !clones ? 1u : 0u) + (arch ? 5u : 0u) + pieces[p].length;
        fits = length + needed <= capacity;
        if (fits)
        {
            if (p != 0 && !clones)
            {
                text[length++] = '_';
            }
            if (arch)
            {
                memcpy(text + length, "arch_", 5);
                length += 5;
            }
            memcpy(text + length, pieces[p].pointer, pieces[p].length);
            length += (uint32_t)pieces[p].length;
        }
    }
    if (fits && clones && option->is_default)
    {
        fits = length + 7 <= capacity;
        if (fits)
        {
            memcpy(text + length, "default", 7);
            length += 7;
        }
    }
    if (fits && clones)
    {
        const int written = snprintf(text + length, capacity - length, ".%u", option->index);
        fits = written > 0 && (uint32_t)written < capacity - length;
        length += fits ? (uint32_t)written : 0;
    }
    return fits ? length : 0;
}

// The versions of a target_clones attribute, in its order (checkTargetClonesAttr's, each string's versions split):
// `count` of them, at most `capacity`, each with its index and whether an earlier one is the same. Whether they fit.
static bool check_program_target_clones(const CheckProgram* program, const CheckInputAttribute* attribute, CheckProgramTargetOption* options, uint32_t capacity,
                                        uint32_t* count)
{
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    bool fits = true;
    *count = 0;
    uint32_t others = 0;
    for (uint32_t token = attribute->argument_first; fits && token < attribute->argument_end; token += 1)
    {
        // Each string literal's text, as the checker decoded it: the attribute's arguments are strings and commas, and
        // a string of several literals is one argument (rare: read as its first literal's).
        const bool string = parser->tokens.kinds[token] == PARSE_KIND_STRING && (token == attribute->argument_first || parser->tokens.kinds[token - 1] != PARSE_KIND_STRING);
        String8 text = { 0 };
        if (string)
        {
            uint32_t end = token;
            while (end < attribute->argument_end && parser->tokens.kinds[end] == PARSE_KIND_STRING)
            {
                end += 1;
            }
            text = check_program_string_text(program, token, end);
            fits = text.pointer != NULL;
        }
        for (uint32_t cursor = 0; fits && string && cursor < text.length;)
        {
            uint32_t end = cursor;
            while (end < text.length && text.pointer[end] != ',')
            {
                end += 1;
            }
            const String8 item = check_target_version_slice(text, cursor, (uint32_t)end);
            cursor = end + 1;
            fits = *count < capacity;
            if (fits)
            {
                CheckProgramTargetOption* option = options + *count;
                *option = (CheckProgramTargetOption){ .is_default = check_input_attribute_equal(item, "default") };
                if (check_target_version_prefix(item, "arch="))
                {
                    option->arch = check_target_version_slice(item, 5, (uint32_t)item.length);
                }
                else if (!option->is_default)
                {
                    option->features[0] = item;
                    option->feature_count = 1;
                }
                option->index = option->is_default ? 0 : others;
                others += option->is_default ? 0 : 1;
                for (uint32_t earlier = 0; earlier < *count; earlier += 1)
                {
                    option->repeated |= check_target_versions_text_equal(options[earlier].arch, option->arch) &&
                                        options[earlier].feature_count == option->feature_count && options[earlier].is_default == option->is_default &&
                                        (option->feature_count == 0 || check_target_versions_text_equal(options[earlier].features[0], option->features[0]));
                }
                *count += 1;
            }
        }
    }
    for (uint32_t c = 0; c < *count; c += 1)
    {
        options[c].index = options[c].is_default ? others : options[c].index;
    }
    (void)input;
    return fits;
}

// The function's name followed by a period and `suffix` (`suffix_length` bytes), or by `tail` where it isn't NULL, in
// `output`.
static String8 check_program_target_name(const CheckProgram* program, uint32_t declaration, const char* suffix, uint32_t suffix_length, const char* tail, Arena* output)
{
    const uint32_t identifier = program->parser->tokens.names[declaration];
    const uint32_t length = intern_length(program->identifiers, identifier);
    const uint64_t extra = tail != NULL ? strlen(tail) : (uint64_t)suffix_length + 1;
    char* text = BUSTER_ARENA_PUSH_ARRAY(output, char, (uint64_t)length + extra);
    if (text != NULL)
    {
        memcpy(text, intern_bytes(program->identifiers, identifier), length);
        if (tail != NULL)
        {
            memcpy(text + length, tail, extra);
        }
        else
        {
            text[length] = '.';
            memcpy(text + length + 1, suffix, suffix_length);
        }
    }
    return (String8){ .pointer = text, .length = text != NULL ? length + extra : 0 };
}

// The symbol name of option `option` of group `group`: f for target's "default", f.SUFFIX otherwise. In `output`.
static String8 check_program_target_option_name(const CheckProgram* program, const CheckProgramTargetGroup* group, const CheckProgramTargetOption* option, Arena* output)
{
    char suffix[512];
    const bool clones = group->clones != 0;
    const uint32_t length = !clones && option->is_default ? 0 : check_program_target_suffix(option, clones, suffix, sizeof(suffix));
    const bool plain = !clones && option->is_default;
    return plain ? check_program_target_name(program, group->first, NULL, 0, "", output) :
           check_program_target_name(program, group->first, suffix, length, NULL, output);
}

// The target attribute of target's version `v`.
static const CheckInputAttribute* check_program_target_version_attribute(const CheckProgram* program, uint32_t v)
{
    return check_target_versions_own(check_input_attributes_at(program->input, program->target_versions[v].declaration), CHECK_ATTRIBUTE_TARGET);
}

// Whether `object` is referenced: by a body (CheckProgram.references) or by an initializer (`initializer_references`).
static bool check_program_target_referenced(const CheckProgram* program, uint32_t object, const uint32_t* initializer_references, uint32_t initializer_reference_count)
{
    bool found = false;
    for (uint32_t b = 0; !found && b < program->body_count; b += 1)
    {
        const CheckProgramBody* body = program->bodies + b;
        for (uint32_t r = 0; !found && program->references != NULL && r < body->reference_count; r += 1)
        {
            found = program->references[body->reference_first + r] == object;
        }
    }
    for (uint32_t r = 0; !found && initializer_references != NULL && r < initializer_reference_count; r += 1)
    {
        found = initializer_references[r] == object;
    }
    return found;
}

// The unit's groups (CheckProgram.target_groups) from the declarations with a target or target_clones attribute among the
// static objects' target events: by name, a group where a declaration has target("default") or target_clones, or two
// objects have target attributes. The options of each resolver are checked here, where their conditions are clang's
// (an arch= whose priority clang leaves undefined is refused by name). In `output`. Returns false where there was no
// room.
static bool check_program_target_groups(CheckProgram* program, const CheckProgramTargetEvents* events, const uint32_t* initializer_references,
                                        uint32_t initializer_reference_count, Arena* output)
{
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const bool named = program->identifiers != NULL;
    const uint32_t count = events->room && named ? events->count : 0;
    if (events->room && events->count != 0 && !named)
    {
        check_program_fail(program, CHECK_PROGRAM_UNSUPPORTED, (uint32_t)events->keys[0]);
    }
    CheckProgramTargetGroup* groups = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramTargetGroup, count) : NULL;
    CheckProgramTargetVersion* versions = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramTargetVersion, count) : NULL;
    bool valid = count == 0 || (groups != NULL && versions != NULL);
    uint32_t group_count = 0;
    uint32_t version_count = 0;
    for (uint32_t e = 0; valid && e < count; e += 1)
    {
        const uint32_t token = (uint32_t)events->keys[e];
        const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
        const bool versioned = check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET) != NULL || check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET_CLONES) != NULL;
        bool known = !versioned;
        for (uint32_t g = 0; !known && g < group_count; g += 1)
        {
            known = parser->tokens.names[groups[g].first] == parser->tokens.names[token];
            groups[g].first = known && token < groups[g].first ? token : groups[g].first;
        }
        if (!known)
        {
            groups[group_count++] = (CheckProgramTargetGroup){ .first = token };
        }
    }
    // Each group's versions, and whether it is multiversioned at all; a name of one plain target function is no group.
    uint32_t kept = 0;
    for (uint32_t g = 0; valid && g < group_count; g += 1)
    {
        CheckProgramTargetGroup group = groups[g];
        group.version_first = version_count;
        bool multiversioned = false;
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t token = (uint32_t)events->keys[e];
            const uint32_t object = (uint32_t)(events->keys[e] >> 32);
            const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
            const CheckInputAttribute* target = check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET);
            const CheckInputAttribute* clones = check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET_CLONES);
            if ((target == NULL && clones == NULL) || parser->tokens.names[token] != parser->tokens.names[group.first])
            {
                continue;
            }
            multiversioned |= clones != NULL || check_target_versions_default(target);
            if (clones != NULL)
            {
                group.clones = object;
                group.clones_declaration = group.clones_declaration == 0 || token < group.clones_declaration ? token : group.clones_declaration;
                continue;
            }
            bool known = false;
            for (uint32_t v = group.version_first; v < version_count; v += 1)
            {
                known |= versions[v].object == object;
                versions[v].declaration = versions[v].object == object && token < versions[v].declaration ? token : versions[v].declaration;
            }
            if (!known)
            {
                versions[version_count++] = (CheckProgramTargetVersion){ .object = object, .declaration = token };
            }
        }
        multiversioned |= version_count - group.version_first > 1;
        group.version_count = version_count - group.version_first;
        if (!multiversioned)
        {
            version_count = group.version_first;
            continue;
        }
        group.internal = check_input_linkage(input->declaration_bits, group.first) == 1;
        // R, or a target_clones body's own symbol's object: the synthetic object at the attribute's name, plus one.
        const uint32_t attributed = group.clones != 0 ? group.clones_declaration : group.first;
        const CheckInputAttributes* attributes = check_input_attributes_at(input, attributed);
        const CheckInputAttribute* attribute = group.clones != 0 ? check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET_CLONES) :
                                                                   check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET);
        const uint32_t synthetic = attribute->name_token + 1;
        program->object_declarations[synthetic] = attributed;
        group.reference = group.clones != 0 ? group.clones : synthetic;
        group.body = group.clones != 0 ? synthetic : 0;
        // Resolved: target's where a version is referenced; target_clones' where defined or referenced.
        bool referenced = false;
        for (uint32_t v = group.version_first; v < version_count; v += 1)
        {
            referenced |= check_program_target_referenced(program, versions[v].object, initializer_references, initializer_reference_count);
        }
        referenced |= group.clones != 0 &&
                      (check_program_cpu_defined(program, group.clones) ||
                       check_program_target_referenced(program, group.clones, initializer_references, initializer_reference_count));
        group.resolved = referenced;
        // The options' conditions, which the CPU model's symbols follow.
        CheckProgramTargetOption options[BUSTER_CHECK_PROGRAM_TARGET_OPTIONS];
        uint32_t option_count = 0;
        bool fits = true;
        if (group.clones != 0)
        {
            fits = check_program_target_clones(program, attribute, options, BUSTER_CHECK_PROGRAM_TARGET_OPTIONS, &option_count);
        }
        for (uint32_t v = 0; group.clones == 0 && fits && v < group.version_count; v += 1)
        {
            fits = check_program_target_parse(check_program_target_version_attribute_of(program, versions, group.version_first + v)->text, options + v);
            option_count += 1;
        }
        for (uint32_t o = 0; fits && o < option_count; o += 1)
        {
            fits = check_program_target_condition(options + o);
            group.model |= options[o].cpu_is || options[o].mask[0] != 0;
            group.features2 |= (options[o].mask[1] | options[o].mask[2] | options[o].mask[3]) != 0;
        }
        if (!fits)
        {
            check_program_fail(program, CHECK_PROGRAM_UNSUPPORTED, attribute->name_token);
        }
        groups[kept++] = group;
    }
    if (valid)
    {
        program->target_groups = groups;
        program->target_group_count = kept;
        program->target_versions = versions;
        program->target_version_count = version_count;
    }
    return valid;
}

// Before the symbols are numbered (check_program_alias_number): R is needed where the group is resolved; a version is
// needed for its own symbol where the program has its body (its references move to R); and a target_clones body's own
// symbol's object where the program has the body.
static void check_program_target_needed(const CheckProgram* program, uint8_t* needed)
{
    for (uint32_t g = 0; g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        for (uint32_t v = group->version_first; v < group->version_first + group->version_count; v += 1)
        {
            const uint32_t object = program->target_versions[v].object;
            needed[object] = check_program_cpu_defined(program, object);
        }
        needed[group->reference] |= group->resolved;
        if (group->body != 0)
        {
            needed[group->body] = check_program_cpu_defined(program, group->clones);
        }
    }
}

// After the symbols are numbered: each version's own symbol and each target_clones body's (CheckProgram.target_symbols,
// in `output`; 0 for none), and every version's references at R's symbol. Returns false where there was no room.
static bool check_program_target_number(CheckProgram* program, Arena* output)
{
    const uint32_t count = program->target_version_count + program->target_group_count;
    SymbolId* symbols = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, count) : NULL;
    const bool valid = count == 0 || symbols != NULL;
    for (uint32_t g = 0; valid && g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        const SymbolId reference = program->object_symbols[group->reference];
        for (uint32_t v = group->version_first; v < group->version_first + group->version_count; v += 1)
        {
            const uint32_t object = program->target_versions[v].object;
            symbols[v] = program->object_symbols[object];
            program->object_symbols[object] = reference;
        }
        symbols[program->target_version_count + g] = group->body != 0 ? program->object_symbols[group->body] : 0;
    }
    program->target_symbols = valid ? symbols : NULL;
    return valid;
}

#endif
