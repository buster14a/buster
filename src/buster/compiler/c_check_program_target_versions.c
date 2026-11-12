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
//   an alias of it, which the unit emits where it defines or references the function. Its body is compiled once per
//   version, as clang compiles it, each copy with that version's instruction set at its version's name (f.avx2.0: the
//   version, arch_CPU for arch=, and its place among the versions that aren't "default", which takes their count): the
//   body itself at the first version's (a synthetic object's symbol at the attribute's name) and copies at symbols after
//   the program's (check_program_target_copies, check_program_target_number). A body whose copies the lowering can't
//   make yet (check_program_target_copy_refused) is compiled once, with the unit's features, valid for every version,
//   and the other versions' names are symbols at its code.
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
    // In the arena the options are in, one entry for each item of the version's string.
    String8* features;
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
// feature as a version's), the features in `arena`. Whether there was room.
static bool check_program_target_parse(String8 text, CheckProgramTargetOption* option, Arena* arena)
{
    *option = (CheckProgramTargetOption){ .is_default = check_input_attribute_equal(text, "default") };
    uint64_t items = 1;
    for (uint64_t i = 0; !option->is_default && i < text.length; i += 1)
    {
        items += text.pointer[i] == ',';
    }
    option->features = option->is_default ? NULL : BUSTER_ARENA_PUSH_ARRAY(arena, String8, items);
    const bool fits = option->is_default || option->features != NULL;
    for (uint32_t cursor = 0; fits && !option->is_default && cursor <= text.length;)
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
        else
        {
            option->features[option->feature_count++] = item;
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

// The resolver's condition and priority of `option`, as clang 23 gives them; whether they are defined. An arch= that
// __builtin_cpu_is doesn't name (corei7-avx, skx: aliases of -march's) is EmitX86CpuIs's default, a test of the model's
// vendor against 0. An arch= processor whose key feature has no priority (c_check_target_versions.inc's -1: knm, the one
// Sema takes) has none in clang's getFMVPriority either, where its getFeaturePriority is unreachable: undefined
// behavior, whose order differs between clang's builds (23.1.4 sorts knm below sse4.2, 23.1.6 above every version), so
// it is refused by name.
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
            option->field = 0;
            option->value = 0;
            (void)check_program_target_cpu_is(option->arch, &option->field, &option->value);
            option->cpu_is = true;
        }
    }
    return valid;
}

// The feature of `option` that a target version's name gives after the one of key `after`: the features by their
// priorities, the highest first, in their order among equals (clang's stable sort), each of key priority << 32 | ~place.
// UINT32_MAX after the last; `after` UINT64_MAX for the first.
static uint32_t check_program_target_next_feature(const CheckProgramTargetOption* option, uint64_t* after)
{
    uint32_t next = UINT32_MAX;
    uint64_t best = 0;
    for (uint32_t f = 0; f < option->feature_count; f += 1)
    {
        const uint64_t key = (uint64_t)check_program_target_feature_priority(option->features[f]) << 32 | (uint64_t)(UINT32_MAX - f);
        const bool better = key < *after && (next == UINT32_MAX || key > best);
        next = better ? f : next;
        best = better ? key : best;
    }
    *after = next != UINT32_MAX ? best : 0;
    return next;
}

// The versions of a target_clones attribute, in its order (checkTargetClonesAttr's, each string's versions split, which
// the checker keeps joined by commas as the attribute's text): `count` of them, at most `capacity`, each with its index
// and whether an earlier one is the same; their features in `arena`. Whether they fit.
static bool check_program_target_clones(const CheckInputAttribute* attribute, CheckProgramTargetOption* options, uint32_t capacity, uint32_t* count, Arena* arena)
{
    const String8 text = attribute->text;
    bool fits = text.pointer != NULL;
    *count = 0;
    uint32_t others = 0;
    for (uint32_t cursor = 0; fits && cursor <= text.length;)
    {
        uint32_t end = cursor;
        while (end < text.length && text.pointer[end] != ',')
        {
            end += 1;
        }
        const String8 item = check_target_version_slice(text, cursor, end);
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
                option->features = BUSTER_ARENA_PUSH_ARRAY(arena, String8, 1);
                fits = option->features != NULL;
                option->feature_count = fits ? 1 : 0;
                if (fits)
                {
                    option->features[0] = item;
                }
            }
            option->index = others;
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
    for (uint32_t c = 0; c < *count; c += 1)
    {
        options[c].index = options[c].is_default ? others : options[c].index;
    }
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

// The symbol name of option `option` of group `group`: f for target's "default", f.SUFFIX otherwise, in `output`. The
// suffix is target's (arch_CPU, then the features by their priorities, the highest first, '_' between) or target_clones'
// (its version, arch_CPU for arch=, then a period and its index). Empty where there was no room.
static String8 check_program_target_option_name(const CheckProgram* program, const CheckProgramTargetGroup* group, const CheckProgramTargetOption* option, Arena* output)
{
    const bool clones = group->clones != 0;
    const bool plain = !clones && option->is_default;
    char index[16];
    const int index_length = clones ? snprintf(index, sizeof(index), ".%u", option->index) : 0;
    const uint32_t identifier = program->parser->tokens.names[group->first];
    const uint32_t identifier_length = intern_length(program->identifiers, identifier);
    // The pieces: arch= first, then the features in their order.
    uint64_t length = identifier_length;
    uint32_t pieces = 0;
    if (!plain && option->arch.length != 0)
    {
        length += 5 + option->arch.length;
        pieces += 1;
    }
    for (uint32_t f = 0; !plain && f < option->feature_count; f += 1)
    {
        length += option->features[f].length;
        pieces += 1;
    }
    length += plain ? 0 : 1u + (clones || pieces == 0 ? 0u : pieces - 1u) + (clones && option->is_default ? 7u : 0u) + (uint32_t)index_length;
    char* text = index_length >= 0 ? BUSTER_ARENA_PUSH_ARRAY(output, char, length) : NULL;
    uint64_t at = 0;
    if (text != NULL)
    {
        memcpy(text, intern_bytes(program->identifiers, identifier), identifier_length);
        at = identifier_length;
        if (!plain)
        {
            text[at++] = '.';
        }
        if (!plain && option->arch.length != 0)
        {
            memcpy(text + at, "arch_", 5);
            memcpy(text + at + 5, option->arch.pointer, option->arch.length);
            at += 5 + option->arch.length;
        }
        uint64_t after = UINT64_MAX;
        for (uint32_t f = plain ? UINT32_MAX : check_program_target_next_feature(option, &after); f != UINT32_MAX; f = check_program_target_next_feature(option, &after))
        {
            if (!clones && (at != identifier_length + 1))
            {
                text[at++] = '_';
            }
            memcpy(text + at, option->features[f].pointer, option->features[f].length);
            at += option->features[f].length;
        }
        if (clones && option->is_default)
        {
            memcpy(text + at, "default", 7);
            at += 7;
        }
        memcpy(text + at, index, (uint64_t)index_length);
        at += (uint64_t)index_length;
    }
    return (String8){ .pointer = text, .length = text != NULL ? at : 0 };
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
// (check_program_target_condition). In `output`. Returns false where there was no room.
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
        // (A static one is emitted only where referenced, as clang defers it.)
        referenced |= group.clones != 0 &&
                      ((check_program_cpu_defined(program, group.clones) && !group.internal) ||
                       check_program_target_referenced(program, group.clones, initializer_references, initializer_reference_count));
        group.resolved = referenced;
        // The options' conditions, which the CPU model's symbols follow: one per version (target's), or per item of
        // target_clones' joined text, which has a comma between two.
        uint32_t room = group.version_count;
        for (uint32_t c = 0; group.clones != 0 && attribute->text.pointer != NULL && c <= attribute->text.length; c += 1)
        {
            room += c == attribute->text.length || attribute->text.pointer[c] == ',';
        }
        CheckProgramTargetOption* options = room != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramTargetOption, room) : NULL;
        valid = room == 0 || options != NULL;
        uint32_t option_count = 0;
        bool fits = valid;
        if (group.clones != 0 && fits)
        {
            fits = check_program_target_clones(attribute, options, room, &option_count, output);
        }
        for (uint32_t v = 0; group.clones == 0 && fits && v < group.version_count; v += 1)
        {
            const CheckInputAttribute* target = check_target_versions_own(check_input_attributes_at(input, versions[group.version_first + v].declaration), CHECK_ATTRIBUTE_TARGET);
            fits = check_program_target_parse(target->text, options + v, output);
            option_count += 1;
        }
        for (uint32_t o = 0; fits && o < option_count; o += 1)
        {
            fits = check_program_target_condition(options + o);
            group.model |= options[o].cpu_is || options[o].mask[0] != 0;
            group.features2 |= (options[o].mask[1] | options[o].mask[2] | options[o].mask[3]) != 0;
        }
        if (valid && !fits)
        {
            check_program_fail(program, CHECK_PROGRAM_UNSUPPORTED, attribute->name_token);
        }
        group.option_count = option_count;
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
// in `output`; 0 for none), and every version's references at R's symbol. And target_clones' copies' symbols
// (CheckProgram.clone_symbols): where a group has copies and the program its body, a symbol after the program's for
// each option but the first and the repeated ones, of the body's object (its declaration's binding), which
// check_program_target_names names. Returns false where there was no room.
// The labels of target_clones group `group`'s body whose addresses the program takes (#2247): those with a symbol, in
// their tokens' order, written to `tokens` where it isn't NULL. Returns their count.
static uint32_t check_program_target_labels(const CheckProgram* program, const CheckProgramTargetGroup* group, uint32_t* tokens)
{
    const Parser* parser = program->parser;
    uint32_t opening = BUSTER_CHECK_NONE;
    for (uint32_t b = 0; opening == BUSTER_CHECK_NONE && b < program->body_count; b += 1)
    {
        opening = program->bodies[b].object == group->clones ? program->bodies[b].opening : opening;
    }
    const uint32_t closing = parser != NULL && opening < parser->tokens.count ? parser->tree.parents[opening] : 0;
    uint32_t count = 0;
    for (uint32_t token = opening; closing > opening && closing < parser->tokens.count && token < closing; token += 1)
    {
        const bool label = parser->tree.roles[token] == PARSE_ROLE_LABEL && token + 1 < program->object_count && program->object_symbols[token + 1] != 0;
        if (label && tokens != NULL)
        {
            tokens[count] = token;
        }
        count += label;
    }
    return count;
}

static bool check_program_target_number(CheckProgram* program, Arena* output)
{
    const uint32_t count = program->target_version_count + program->target_group_count;
    SymbolId* symbols = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, count) : NULL;
    bool valid = count == 0 || symbols != NULL;
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
    // The copies' symbols, numbered after the program's.
    SymbolId* clone_symbols = valid && program->clone_option_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, program->clone_option_count) : NULL;
    valid = valid && (program->clone_option_count == 0 || clone_symbols != NULL);
    program->clone_symbols = clone_symbols;
    uint32_t extra = 0;
    for (uint32_t g = 0; valid && clone_symbols != NULL && g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        const bool copied = group->clones != 0 && group->copies && symbols[program->target_version_count + g] != 0;
        for (uint32_t o = 0; group->clones != 0 && o < group->option_count; o += 1)
        {
            const bool own = copied && program->clone_copied[group->clone_first + o] != 0;
            clone_symbols[group->clone_first + o] = own ? program->symbol_count + 1 + extra : 0;
            extra += own;
        }
    }
    // The copies' labels whose addresses the program takes (#2247): each option with code but the last one's, a symbol
    // of its own for each, after the copies'; the last one's code defines the labels' own symbols, which the program's
    // data reaches, as clang's static data reaches the last clone it emits (in the options' order).
    uint32_t labels = 0;
    for (uint32_t g = 0; valid && clone_symbols != NULL && g < program->target_group_count; g += 1)
    {
        CheckProgramTargetGroup* group = program->target_groups + g;
        group->label_count = group->clones != 0 && group->copies && symbols[program->target_version_count + g] != 0 ?
                             check_program_target_labels(program, group, NULL) : 0;
        group->label_first = labels;
        labels += group->label_count;
    }
    uint32_t* label_tokens = labels != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, labels) : NULL;
    SymbolId* label_symbols = labels != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, program->clone_option_count) : NULL;
    valid = valid && (labels == 0 || (label_tokens != NULL && label_symbols != NULL));
    for (uint32_t g = 0; valid && labels != 0 && g < program->target_group_count; g += 1)
    {
        CheckProgramTargetGroup* group = program->target_groups + g;
        uint32_t last = group->option_count;
        for (uint32_t o = 0; group->label_count != 0 && o < group->option_count; o += 1)
        {
            last = o == 0 || program->clone_copied[group->clone_first + o] != 0 ? o : last;
        }
        if (group->label_count != 0)
        {
            (void)check_program_target_labels(program, group, label_tokens + group->label_first);
        }
        for (uint32_t o = 0; group->clones != 0 && o < group->option_count; o += 1)
        {
            const bool own = group->label_count != 0 && o != last && (o == 0 || program->clone_copied[group->clone_first + o] != 0);
            label_symbols[group->clone_first + o] = own ? program->symbol_count + 1 + extra : 0;
            extra += own ? group->label_count : 0;
        }
    }
    program->clone_label_symbols = labels != 0 ? label_symbols : NULL;
    program->clone_label_tokens = labels != 0 ? label_tokens : NULL;
    uint32_t* objects = valid && extra != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)program->symbol_count + extra) : NULL;
    valid = valid && (extra == 0 || objects != NULL);
    if (valid && extra != 0)
    {
        if (program->symbol_count != 0)
        {
            memcpy(objects, program->symbol_objects, (uint64_t)program->symbol_count * sizeof(uint32_t));
        }
        for (uint32_t g = 0; g < program->target_group_count; g += 1)
        {
            const CheckProgramTargetGroup* group = program->target_groups + g;
            for (uint32_t o = 0; group->clones != 0 && o < group->option_count; o += 1)
            {
                const SymbolId symbol = clone_symbols[group->clone_first + o];
                if (symbol != 0)
                {
                    objects[symbol - 1] = group->body;
                }
                // (A copy's label's symbol is the label's object's too: its name is made as the label's, #2247.)
                const SymbolId label = program->clone_label_symbols != NULL ? program->clone_label_symbols[group->clone_first + o] : 0;
                for (uint32_t l = 0; label != 0 && l < group->label_count; l += 1)
                {
                    objects[label - 1 + l] = program->clone_label_tokens[group->label_first + l] + 1;
                }
            }
        }
        program->symbol_objects = objects;
        program->symbol_count += extra;
    }
    return valid;
}

// The symbol body `b` is defined at for these groups: a version's own, or a target_clones body's; 0 for another body.
static SymbolId check_program_target_body_symbol(const CheckProgram* program, uint32_t b)
{
    const uint32_t object = program->bodies[b].object;
    SymbolId symbol = 0;
    for (uint32_t v = 0; program->target_symbols != NULL && v < program->target_version_count; v += 1)
    {
        symbol = program->target_versions[v].object == object ? program->target_symbols[v] : symbol;
    }
    // A target_clones body's, or its copy's (CheckProgram.body_options).
    const uint32_t option = program->body_options != NULL ? program->body_options[b] : 0;
    for (uint32_t g = 0; program->target_symbols != NULL && g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        const bool own = group->clones != 0 && group->clones == object;
        const SymbolId copy = own && option != 0 && program->clone_symbols != NULL ? program->clone_symbols[group->clone_first + option] : 0;
        symbol = own ? (copy != 0 ? copy : program->target_symbols[program->target_version_count + g]) : symbol;
    }
    return symbol;
}

// A group's options (CheckProgramTargetGroup.option_count of them), in `arena`, from its versions or its target_clones
// attribute (check_program_target_groups checked that they fit and that their conditions are defined), or NULL where
// there was no room.
static CheckProgramTargetOption* check_program_target_options(const CheckProgram* program, const CheckProgramTargetGroup* group, Arena* arena)
{
    const uint32_t room = group->option_count;
    CheckProgramTargetOption* options = room != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckProgramTargetOption, room) : NULL;
    uint32_t count = 0;
    if (options != NULL && group->clones != 0)
    {
        const CheckInputAttribute* attribute = check_target_versions_own(check_input_attributes_at(program->input, group->clones_declaration), CHECK_ATTRIBUTE_TARGET_CLONES);
        (void)check_program_target_clones(attribute, options, room, &count, arena);
    }
    for (uint32_t v = 0; options != NULL && group->clones == 0 && v < group->version_count && v < room; v += 1)
    {
        (void)check_program_target_parse(check_program_target_version_attribute(program, group->version_first + v)->text, options + v, arena);
        count += 1;
    }
    for (uint32_t o = 0; options != NULL && o < count; o += 1)
    {
        (void)check_program_target_condition(options + o);
    }
    return options;
}

// Whether target_clones' body `b` is one the copies leave as it was, compiled once (#1838): a naked function's, whose
// templates are its whole code, planned once per body (check_program_asm_plan_naked). (A switch's jump tables are each
// copy's own, check_lower_switch_tables, and so are its labels' symbols, check_program_target_number, and its asm
// statements' plans, made for the copy's instruction set, check_program_asm_plan_of, #2247.)
static bool check_program_target_copy_refused(const CheckProgram* program, uint32_t b)
{
    const CheckProgramBody* body = program->bodies + b;
    return program->parser == NULL || (program->object_naked != 0 && body->object < program->object_count && program->object_naked[body->object] <= body->declaration);
}

// target_clones' body copies (#1838), as clang compiles its body once per version, with that version's instruction set:
// for each group whose body the program has (CheckProgramTargetGroup.copies, unless check_program_target_copy_refused),
// each option's instruction set (CheckProgram.clone_targets: 0 for "default", the unit's; otherwise a target of the
// version's string, arch= its CPU's, made as check_program_targets makes a target attribute's) and the body's own, its
// first option's (CheckProgram.body_targets). Called at the end of check_program_targets, for a unit with a group.
// Returns false where there was no room.
static bool check_program_target_copies(CheckProgram* program, const PredefinesOptions* predefines, Arena* output)
{
    uint32_t total = 0;
    for (uint32_t g = 0; g < program->target_group_count; g += 1)
    {
        total += program->target_groups[g].clones != 0 ? program->target_groups[g].option_count : 0;
    }
    bool valid = true;
    uint32_t* clone_targets = total != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, total) : NULL;
    uint8_t* clone_copied = total != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, total) : NULL;
    X86Target* targets = total != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, X86Target, (uint64_t)program->target_count + total) : NULL;
    valid = total == 0 || (clone_targets != NULL && clone_copied != NULL && targets != NULL);
    if (valid && total != 0)
    {
        memset(clone_targets, 0, (uint64_t)total * sizeof(uint32_t));
        memset(clone_copied, 0, total);
        program->clone_copied = clone_copied;
        if (program->target_count != 0)
        {
            memcpy(targets, program->targets, (uint64_t)program->target_count * sizeof(X86Target));
        }
        program->targets = targets;
        program->clone_targets = clone_targets;
        program->clone_option_count = total;
        if (!program->host_read)
        {
            program->host = predefines_host_of(predefines);
            program->host_read = true;
        }
    }
    uint32_t at = 0;
    for (uint32_t g = 0; valid && g < program->target_group_count; g += 1)
    {
        CheckProgramTargetGroup* group = program->target_groups + g;
        if (group->clones == 0)
        {
            continue;
        }
        group->clone_first = at;
        at += group->option_count;
        uint32_t body = program->body_count;
        for (uint32_t b = 0; body == program->body_count && b < program->body_count; b += 1)
        {
            body = program->bodies[b].object == group->clones ? b : body;
        }
        group->copies = body < program->body_count && group->option_count > 1 && !check_program_target_copy_refused(program, body);
        const CheckProgramTargetOption* options = group->copies ? check_program_target_options(program, group, output) : NULL;
        valid = !group->copies || options != NULL;
        for (uint32_t o = 0; valid && options != NULL && o < group->option_count; o += 1)
        {
            const CheckProgramTargetOption* option = options + o;
            program->clone_copied[group->clone_first + o] = o != 0 && !option->repeated;
            // The version's string: arch=CPU, or its feature.
            char text[128];
            const int length = option->is_default ? 0 :
                               option->arch.length != 0 ? snprintf(text, sizeof(text), "arch=%.*s", (int)option->arch.length, option->arch.pointer) :
                                                          snprintf(text, sizeof(text), "%.*s", (int)option->features[0].length, option->features[0].pointer);
            int32_t cpu = -1;
            const bool version = length > 0 && length < (int)sizeof(text) &&
                                 predefines_target_attribute(text, (uint32_t)length, &cpu) == PREDEFINES_TARGET_ATTRIBUTE_VERSION;
            if (version)
            {
                program->targets[program->target_count] = predefines_function_target(predefines, &program->host, program->target, text, (uint32_t)length, cpu);
                program->target_count += 1;
                program->clone_targets[group->clone_first + o] = program->target_count;
            }
        }
        if (valid && group->copies && program->body_targets == NULL)
        {
            program->body_targets = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, program->body_count);
            valid = program->body_targets != NULL;
            if (valid)
            {
                memset(program->body_targets, 0, (uint64_t)program->body_count * sizeof(uint32_t));
            }
        }
        if (valid && group->copies)
        {
            program->body_targets[body] = program->clone_targets[group->clone_first];
        }
    }
    return valid;
}

// The names of the groups' symbols before the emission adds them (`names` by symbol, CheckProgramCode's symbol_names):
// R's f.ifunc, each version's own (f for "default"), and each target_clones body's (its first version's name). Returns
// false where there was no room.
static bool check_program_target_names(const CheckProgram* program, String8* names, Arena* output)
{
    bool valid = true;
    for (uint32_t g = 0; valid && g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        const CheckProgramTargetOption* options = check_program_target_options(program, group, output);
        const uint32_t count = options != NULL ? group->option_count : 0;
        valid = options != NULL || group->option_count == 0;
        const SymbolId reference = program->object_symbols[group->reference];
        if (valid && group->clones == 0 && reference != 0 && program->symbol_objects[reference - 1] == group->reference)
        {
            names[reference] = check_program_target_name(program, group->first, NULL, 0, ".ifunc", output);
            valid = names[reference].length != 0;
        }
        for (uint32_t v = 0; valid && group->clones == 0 && v < group->version_count && v < count; v += 1)
        {
            const SymbolId own = program->target_symbols != NULL ? program->target_symbols[group->version_first + v] : 0;
            if (own != 0)
            {
                names[own] = check_program_target_option_name(program, group, options + v, output);
                valid = names[own].length != 0;
            }
        }
        const SymbolId body = group->clones != 0 && program->target_symbols != NULL ? program->target_symbols[program->target_version_count + g] : 0;
        if (valid && body != 0 && count != 0)
        {
            names[body] = check_program_target_option_name(program, group, options, output);
            valid = names[body].length != 0;
        }
        // Its copies', each its version's.
        for (uint32_t o = 1; valid && group->clones != 0 && program->clone_symbols != NULL && o < count; o += 1)
        {
            const SymbolId copy = program->clone_symbols[group->clone_first + o];
            if (copy != 0)
            {
                names[copy] = check_program_target_option_name(program, group, options + o, output);
                valid = names[copy].length != 0;
            }
        }
    }
    return valid;
}

// The symbol whose name a block-scope static of function `object` takes before its own, where `object` is one of the
// groups' (clang names it by its function's mangled name where clang emits the function): a target version's own
// (s.avx512f.n) where the function was multiversioned there, as a static group's are when its deferred versions are
// emitted, or as one declared after another version is; otherwise none, the function's identifier alone (f.n, a version
// defined before the function became multiversioned). A target_clones function's is its body's first version's
// (c.avx2.0.n). Whether `object` is one of theirs.
static bool check_program_target_static_context(const CheckProgram* program, uint32_t object, SymbolId* symbol)
{
    bool found = false;
    *symbol = 0;
    for (uint32_t g = 0; !found && g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        for (uint32_t v = group->version_first; !found && v < group->version_first + group->version_count; v += 1)
        {
            found = program->target_versions[v].object == object;
            bool later = group->internal;
            for (uint32_t other = group->version_first; found && other < group->version_first + group->version_count; other += 1)
            {
                later |= other != v && program->target_versions[other].declaration < program->target_versions[v].declaration;
            }
            *symbol = found && later && program->target_symbols != NULL ? program->target_symbols[v] : 0;
        }
    }
    for (uint32_t g = 0; !found && g < program->target_group_count; g += 1)
    {
        found = program->target_groups[g].clones != 0 && program->target_groups[g].clones == object;
        *symbol = found && program->target_symbols != NULL ? program->target_symbols[program->target_version_count + g] : 0;
    }
    return found;
}

// The link room the groups' code takes beyond the program's symbols: the resolvers' atoms; their symbols (the resolver,
// f.ifunc's alias, each option's version) and their fixups.
static void check_program_target_room(const CheckProgram* program, uint64_t* atoms, uint64_t* symbols, uint64_t* fixups)
{
    for (uint32_t g = 0; g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        const uint32_t count = group->option_count;
        *atoms += group->resolved;
        *symbols += 2 + count;
        *fixups += group->resolved ? 1 + 6 * (uint64_t)count : 0;
    }
}

// The resolvers' unwind entries (one per group whose resolver the unit emits, check_program_target_atoms), which the
// unwind table writes after the functions' and the helpers'.
static uint32_t check_program_target_frames(const CheckProgram* program)
{
    uint32_t frames = 0;
    for (uint32_t g = 0; g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        frames += group->resolved && program->object_symbols[group->reference] != 0;
    }
    return frames;
}

// The symbol of the version of `option` in group `group`: its own (target's version with a body, CheckProgram.
// target_symbols, or a target_clones body's first name) or one of the link's from `first` on, or 0.
static SymbolId check_program_target_symbol(const Link* link, SymbolId first, String8 name)
{
    return check_program_cpu_find(link, first, name);
}

// The groups' symbols beyond the program's, once the program's, the helpers', the runtime's and the templates' exist and
// before an executable's runtime joins (bind.c): each target_clones body's other versions' names (of its own symbol's
// flags), and for each resolved group, its resolver f.resolver (a weak function, local in a static group), each of its
// options' versions the unit doesn't define (undefined globals), R or f made an IFUNC of the resolver's binding, and
// target_clones' f.ifunc, another. Names in `output`. Returns false with `code`'s status and reason set.
static bool check_program_target_symbols(CheckProgramCode* code, const CheckProgram* program, Arena* output)
{
    Link* link = &code->link;
    bool valid = true;
    code->target_first = link->symbols.count;
    for (uint32_t g = 0; valid && g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        const CheckProgramTargetOption* options = check_program_target_options(program, group, output);
        valid = options != NULL || group->option_count == 0;
        const uint32_t count = options != NULL ? group->option_count : 0;
        const SymbolId body = group->clones != 0 && program->target_symbols != NULL ? program->target_symbols[program->target_version_count + g] : 0;
        // The other versions' names at a target_clones body.
        for (uint32_t o = 1; valid && body != 0 && o < count; o += 1)
        {
            // (A version of a copy of its own has its symbol, the copy's.)
            const bool copied = program->clone_symbols != NULL && program->clone_symbols[group->clone_first + o] != 0;
            if (!options[o].repeated && !copied)
            {
                valid = check_program_cpu_add(link, check_program_target_option_name(program, group, options + o, output), link->symbols.flags[body], true) != 0;
            }
        }
        const SymbolId reference = program->object_symbols[group->reference];
        if (!valid || !group->resolved || reference == 0)
        {
            continue;
        }
        const uint8_t binding = group->internal ? BUSTER_LINK_SYMBOL_LOCAL : BUSTER_LINK_SYMBOL_WEAK;
        valid = check_program_cpu_add(link, check_program_target_name(program, group->first, NULL, 0, ".resolver", output), binding | BUSTER_LINK_SYMBOL_FUNCTION, true) != 0;
        for (uint32_t o = 0; valid && o < count; o += 1)
        {
            const String8 name = check_program_target_option_name(program, group, options + o, output);
            valid = name.length != 0 && (options[o].repeated || check_program_target_symbol(link, 1, name) != 0 ||
                                         check_program_cpu_add(link, name, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, false) != 0);
        }
        if (valid && group->clones != 0)
        {
            valid = check_program_cpu_add(link, check_program_target_name(program, group->first, NULL, 0, ".ifunc", output), binding | BUSTER_LINK_SYMBOL_IFUNC, true) != 0;
        }
        if (valid)
        {
            link->symbols.flags[reference] = (uint8_t)((link->symbols.flags[reference] & BUSTER_LINK_SYMBOL_VISIBILITY) | binding | BUSTER_LINK_SYMBOL_IFUNC);
            link->symbols.source[reference] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    if (!valid)
    {
        code->status = CHECK_PROGRAM_RESOURCE;
        code->reason = "a function multiversioned by target strings couldn't have its symbols added";
    }
    return valid;
}

// Appends the test of one word of the CPU model's features to a resolver's code: `mov eax, [rip + word]` (a PC32 fixup
// to __cpu_model + 12 or __cpu_features2 + 4 * (w - 1)), `and eax, mask`, `cmp eax, mask`, `jne next`, whose
// displacement's offset goes to `branches`.
static void check_program_target_word(uint8_t* text, uint32_t* length, uint32_t w, uint32_t mask, SymbolId model, SymbolId features, uint32_t* fixup_offsets,
                                      uint8_t* fixup_kinds, SymbolId* fixup_targets, int32_t* fixup_addends, uint32_t* fixups, uint32_t* branches,
                                      uint32_t* branch_count)
{
    static const uint8_t test[] = { 0x8b, 0x05, 0, 0, 0, 0, 0x25, 0, 0, 0, 0, 0x3d, 0, 0, 0, 0, 0x0f, 0x85, 0, 0, 0, 0 };
    check_program_cpu_bytes(text, length, test, sizeof(test));
    fixup_offsets[*fixups] = *length - 20;
    fixup_kinds[*fixups] = BUSTER_LINK_FIXUP_PC32;
    fixup_targets[*fixups] = w == 0 ? model : features;
    fixup_addends[*fixups] = (w == 0 ? 12 : 4 * ((int32_t)w - 1)) - 4;
    *fixups += 1;
    check_program_cpu_u32(text, *length - 15, mask);
    check_program_cpu_u32(text, *length - 10, mask);
    branches[(*branch_count)++] = *length - 4;
}

// The groups' code once the functions' atoms exist (after check_program_target_symbols): each target_clones body's other
// versions' names at its body; and each resolved group's resolver, in `bytes`, with f.resolver and R (or f and f.ifunc)
// at it. The resolver is clang's EmitX86MultiVersionResolver in other instructions:
//
//     push rax                                  ; the stack aligned for the call
//     call __cpu_indicator_init                 ; PLT32
//   for each option, in clang's order: its arch='s test, `cmp dword [rip + __cpu_model + 4 * field], value` and
//   `jne next` (a microarchitecture level's is a feature word's), then each nonzero word of its features' mask:
//     mov eax, [rip + word]                     ; PC32 (0: __cpu_model + 12, 1 to 3: __cpu_features2 + 4 * (w - 1))
//     and eax, mask
//     cmp eax, mask
//     jne next
//     lea rax, [rip + f.X]                      ; PC32 to a version the unit defines,
//     (mov rax, [rip + f.X@GOTPCREL])           ; REX_GOTPCRELX to one it doesn't
//     pop rcx
//     ret
//   next: ...
//     ud2                                       ; no option's condition: clang's trap
//
// An option of no condition is taken unconditionally (clang's default, which sorts last), and none after it is reached.
// Returns false with `code`'s status and reason set.
static bool check_program_target_atoms(CheckProgramCode* code, const CheckProgram* program, UnwindFunction* frames, SymbolId* frame_symbols, Arena* bytes)
{
    Link* link = &code->link;
    bool valid = true;
    uint32_t frame_count = 0;
    for (uint32_t g = 0; valid && g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        const CheckProgramTargetOption* options = check_program_target_options(program, group, bytes);
        valid = options != NULL || group->option_count == 0;
        const uint32_t count = options != NULL ? group->option_count : 0;
        const SymbolId body = group->clones != 0 && program->target_symbols != NULL ? program->target_symbols[program->target_version_count + g] : 0;
        const AtomId body_atom = body != 0 && body < link->symbols.count ? link->symbols.atom[body] : 0;
        for (uint32_t o = 1; valid && body_atom != 0 && o < count; o += 1)
        {
            const bool skipped = options[o].repeated || (program->clone_symbols != NULL && program->clone_symbols[group->clone_first + o] != 0);
            const SymbolId symbol = skipped ? 0 : check_program_cpu_find(link, code->target_first, check_program_target_option_name(program, group, options + o, bytes));
            valid = skipped || (symbol != 0 && link_define_symbol(link, symbol, body_atom, link->symbols.value[body], link->symbols.size[body]));
        }
        const SymbolId reference = program->object_symbols[group->reference];
        if (!valid || !group->resolved || reference == 0)
        {
            continue;
        }
        // The options with their versions' symbols, without target_clones' repeated ones, in clang's stable order by
        // priority, the highest first.
        CheckProgramTargetOption* sorted = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(bytes, CheckProgramTargetOption, count) : NULL;
        valid = count == 0 || sorted != NULL;
        uint32_t sorted_count = 0;
        for (uint32_t o = 0; valid && o < count; o += 1)
        {
            if (options[o].repeated)
            {
                continue;
            }
            CheckProgramTargetOption option = options[o];
            const String8 name = check_program_target_option_name(program, group, &option, bytes);
            option.symbol = name.length != 0 ? check_program_target_symbol(link, 1, name) : 0;
            valid = option.symbol != 0;
            uint32_t at = sorted_count;
            while (at > 0 && sorted[at - 1].priority < option.priority)
            {
                sorted[at] = sorted[at - 1];
                at -= 1;
            }
            sorted[at] = option;
            sorted_count += 1;
        }
        const SymbolId indicator = code->lowered.runtime_symbols[CHECK_LOWER_RUNTIME_CPU_INDICATOR_INIT];
        const SymbolId model = code->lowered.runtime_symbols[CHECK_LOWER_RUNTIME_CPU_MODEL];
        const SymbolId features = code->lowered.runtime_symbols[CHECK_LOWER_RUNTIME_CPU_FEATURES2];
        const String8 resolver_name = check_program_target_name(program, group->first, NULL, 0, ".resolver", bytes);
        const SymbolId resolver = resolver_name.length != 0 ? check_program_cpu_find(link, code->target_first, resolver_name) : 0;
        valid = valid && indicator != 0 && (!group->model || model != 0) && (!group->features2 || features != 0) && resolver != 0;
        // The code: 6 bytes for the entry; for each option, 16 for its arch='s test, 22 for each mask word and 9 for its
        // return; and 2.
        const uint32_t capacity = 6 + sorted_count * (16 + 4 * 22 + 9) + 2;
        uint8_t* text = valid ? arena_push(bytes, capacity, 16) : NULL;
        valid = text != NULL;
        // The fixups, in offset order: the call, and for each option at most its arch='s test, four words and its version.
        const uint64_t fixup_room = 1 + 6 * (uint64_t)sorted_count;
        uint32_t* fixup_offsets = valid ? BUSTER_ARENA_PUSH_ARRAY(bytes, uint32_t, fixup_room) : NULL;
        uint8_t* fixup_kinds = fixup_offsets != NULL ? BUSTER_ARENA_PUSH_ARRAY(bytes, uint8_t, fixup_room) : NULL;
        SymbolId* fixup_targets = fixup_kinds != NULL ? BUSTER_ARENA_PUSH_ARRAY(bytes, SymbolId, fixup_room) : NULL;
        int32_t* fixup_addends = fixup_targets != NULL ? BUSTER_ARENA_PUSH_ARRAY(bytes, int32_t, fixup_room) : NULL;
        valid = valid && fixup_addends != NULL;
        // Its unwind entry's rows (clang's CFI): the CFA at rsp + 16 after the push, at rsp + 8 after each return's pop,
        // and at rsp + 16 again where the next test (or the trap) begins.
        EmissionFrameRow* rows = valid && frames != NULL ? BUSTER_ARENA_PUSH_ARRAY(bytes, EmissionFrameRow, 1 + 2 * (uint64_t)sorted_count) : NULL;
        valid = valid && (frames == NULL || rows != NULL);
        uint32_t row_count = 0;
        uint32_t fixups = 0;
        uint32_t length = 0;
        if (valid)
        {
            static const uint8_t entry[] = { 0x50, 0xe8, 0, 0, 0, 0 };
            check_program_cpu_bytes(text, &length, entry, sizeof(entry));
            fixup_offsets[fixups] = 2;
            fixup_kinds[fixups] = BUSTER_LINK_FIXUP_CALL;
            fixup_targets[fixups] = indicator;
            fixup_addends[fixups] = -4;
            fixups += 1;
            if (rows != NULL)
            {
                rows[row_count++] = (EmissionFrameRow){ .offset = 1, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 };
            }
        }
        bool taken = false;
        for (uint32_t o = 0; valid && !taken && o < sorted_count; o += 1)
        {
            const CheckProgramTargetOption* option = sorted + o;
            uint32_t branches[5];
            uint32_t branch_count = 0;
            if (rows != NULL && o != 0)
            {
                rows[row_count++] = (EmissionFrameRow){ .offset = length, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 };
            }
            if (option->cpu_is)
            {
                // cmp dword [rip + __cpu_model + 4 * field], value; jne next.
                static const uint8_t compare[] = { 0x81, 0x3d, 0, 0, 0, 0, 0, 0, 0, 0, 0x0f, 0x85, 0, 0, 0, 0 };
                check_program_cpu_bytes(text, &length, compare, sizeof(compare));
                fixup_offsets[fixups] = length - 14;
                fixup_kinds[fixups] = BUSTER_LINK_FIXUP_PC32;
                fixup_targets[fixups] = model;
                fixup_addends[fixups] = 4 * (int32_t)option->field - 8;
                fixups += 1;
                check_program_cpu_u32(text, length - 10, option->value);
                branches[branch_count++] = length - 4;
            }
            for (uint32_t w = 0; w < 4; w += 1)
            {
                if (option->mask[w] != 0)
                {
                    check_program_target_word(text, &length, w, option->mask[w], model, features, fixup_offsets, fixup_kinds, fixup_targets, fixup_addends, &fixups,
                                              branches, &branch_count);
                }
            }
            const bool local = link->symbols.source[option->symbol] == BUSTER_LINK_SOURCE_PROGRAM;
            const uint8_t address[] = { 0x48, local ? 0x8d : 0x8b, 0x05, 0, 0, 0, 0, 0x59, 0xc3 };
            check_program_cpu_bytes(text, &length, address, sizeof(address));
            fixup_offsets[fixups] = length - 6;
            fixup_kinds[fixups] = local ? BUSTER_LINK_FIXUP_PC32 : BUSTER_LINK_FIXUP_LOAD_GOT;
            fixup_targets[fixups] = option->symbol;
            fixup_addends[fixups] = -4;
            fixups += 1;
            if (rows != NULL)
            {
                rows[row_count++] = (EmissionFrameRow){ .offset = length - 1, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 8 };
            }
            for (uint32_t b = 0; b < branch_count; b += 1)
            {
                check_program_cpu_u32(text, branches[b], length - (branches[b] + 4));
            }
            taken = branch_count == 0;
        }
        if (valid && !taken)
        {
            if (rows != NULL && sorted_count != 0)
            {
                rows[row_count++] = (EmissionFrameRow){ .offset = length, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 };
            }
            static const uint8_t trap[] = { 0x0f, 0x0b };
            check_program_cpu_bytes(text, &length, trap, sizeof(trap));
        }
        const AtomId atom = valid ? link_add_atom(link, BUSTER_LINK_CLASS_TEXT, 4, length, text, 0) : 0;
        valid = atom != 0;
        for (uint32_t f = 0; valid && f < fixups; f += 1)
        {
            valid = link_add_fixup(link, atom, fixup_offsets[f], fixup_kinds[f], fixup_targets[f], fixup_addends[f]) != 0;
        }
        const SymbolId alias = valid && group->clones != 0 ? check_program_cpu_find(link, code->target_first, check_program_target_name(program, group->first, NULL, 0, ".ifunc", bytes)) : 0;
        const SymbolId defined[3] = { resolver, reference, alias };
        for (uint32_t d = 0; valid && d < 3; d += 1)
        {
            valid = defined[d] == 0 ? d == 2 && group->clones == 0 : link_define_symbol(link, defined[d], atom, 0, length);
        }
        if (valid && frames != NULL)
        {
            frames[frame_count] = (UnwindFunction){ .size = length, .open = BUSTER_UNWIND_ROWS, .rows = rows, .row_count = row_count };
            frame_symbols[frame_count] = resolver;
            frame_count += 1;
        }
    }
    if (!valid)
    {
        code->status = CHECK_PROGRAM_BACKEND;
        code->reason = "a resolver of a function multiversioned by target strings couldn't be emitted";
    }
    return valid;
}

#endif
