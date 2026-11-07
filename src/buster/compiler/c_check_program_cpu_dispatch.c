// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The code clang 23 generates for cpu_specific and cpu_dispatch multiversioning on x86-64 Linux (#1447;
// CodeGenModule::emitCPUDispatchDefinition, CodeGenFunction::EmitX86MultiVersionResolver and EmitX86CpuSupports at
// llvmorg-23.1.4, read for their rules and written again in C). The checker keeps each version apart (its CPU list's own
// object, c_check_cpu_dispatch.c), and a function name's versions and dispatcher form a group:
//
// - A version's body is its own symbol, its first CPU's mangled name (f.M for core_2_duo_ssse3: the name, a period and
//   the CPU's mangling character, c_check_cpu_dispatch.inc), compiled with the unit's features and the features every
//   CPU of its list has (check_program_cpu_version_target); its other CPUs' names are symbols at the same code, where
//   clang compiles a copy for each.
// - Every reference to the function, a call or an address, is to its group's reference object R, a synthetic object at
//   its first declaration's attribute name, whose symbol is f.ifunc: the versions' and the dispatcher's objects take R's
//   symbol as a weakref takes its target's (check_program_cpu_number).
// - A dispatcher in the unit gives the resolver f.resolver, weak, which calls __cpu_indicator_init and returns the first
//   of its CPUs' versions whose features libgcc's CPU model has, in clang's order (by their masks, the most first, a
//   version of no testable feature last); and f.ifunc and f, weak IFUNCs at it. Its body, which clang ignores, isn't
//   emitted (check_program_openings leaves it out). Without one, f.ifunc is the undefined symbol the calls reach.
//
// The groups are rare (a unit without the attributes never makes one), a group has a few versions and a dispatcher of a
// few CPUs, and each step reads those few records: scalar, as a vector pass would set up more than it saves.
#ifndef BUSTER_C_CHECK_PROGRAM_CPU_DISPATCH_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_CPU_DISPATCH_INCLUDED

// The CPUs a cpu_specific or cpu_dispatch attribute names, as rows of check_cpu_dispatch_rows: `count` of them, at most
// `capacity`, in order. The checker made sure each is a row's.
static uint32_t check_program_cpu_rows(const CheckProgram* program, const CheckInputAttribute* attribute, int32_t* rows, uint32_t capacity)
{
    const Parser* parser = program->parser;
    uint32_t count = 0;
    for (uint32_t token = check_cpu_dispatch_first(parser, attribute); token < attribute->argument_end && count < capacity;
         token = check_cpu_dispatch_next(parser, attribute, token))
    {
        const uint32_t name = parser->tokens.names[token];
        const String8 spelling = { .pointer = (char*)intern_bytes(program->identifiers, name), .length = intern_length(program->identifiers, name) };
        const int32_t row = check_cpu_dispatch_row(spelling);
        if (row >= 0)
        {
            rows[count++] = row;
        }
    }
    return count;
}

#define BUSTER_CHECK_PROGRAM_CPU_LIST 64u

// The groups of the unit's cpu_specific and cpu_dispatch functions (CheckProgram.cpu_groups), from the declarations
// with the attributes among the static objects' target events: a version per object of a cpu_specific declaration, the
// dispatcher's object, and the group's reference object R (the attribute's name token of its first declaration, plus
// one), whose declaration is that first one's (its linkage and kind). In `output`. Returns false where there was no room.
static bool check_program_cpu_groups(CheckProgram* program, const CheckProgramTargetEvents* events, Arena* output)
{
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    bool valid = true;
    // At most a group and a version per event.
    // The CPUs' names are read from the identifiers, without which the program refuses the attributes.
    const bool named = program->identifiers != NULL;
    const uint32_t count = input->cpu_dispatch && events->room && named ? events->count : 0;
    if (input->cpu_dispatch && events->room && events->count != 0 && !named)
    {
        check_program_fail(program, CHECK_PROGRAM_UNSUPPORTED, (uint32_t)events->keys[0]);
    }
    CheckProgramCpuGroup* groups = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramCpuGroup, count) : NULL;
    CheckProgramCpuVersion* versions = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramCpuVersion, count) : NULL;
    valid = count == 0 || (groups != NULL && versions != NULL);
    uint32_t group_count = 0;
    uint32_t version_count = 0;
    // The groups first, by their names, each from its first declaration with an attribute in token order.
    for (uint32_t e = 0; valid && e < count; e += 1)
    {
        const uint32_t token = (uint32_t)events->keys[e];
        const CheckInputAttribute* attribute = check_cpu_dispatch_attribute(check_input_attributes_at(input, token));
        uint32_t g = group_count;
        for (uint32_t k = 0; attribute != NULL && k < group_count; k += 1)
        {
            g = parser->tokens.names[groups[k].first] == parser->tokens.names[token] ? k : g;
        }
        if (attribute != NULL && g == group_count)
        {
            groups[group_count++] = (CheckProgramCpuGroup){ .first = token, .dispatch_declaration = BUSTER_CHECK_NONE };
        }
        else if (attribute != NULL && token < groups[g].first)
        {
            groups[g].first = token;
        }
    }
    for (uint32_t g = 0; valid && g < group_count; g += 1)
    {
        CheckProgramCpuGroup* group = groups + g;
        group->version_first = version_count;
        const CheckInputAttribute* first = check_cpu_dispatch_attribute(check_input_attributes_at(input, group->first));
        group->reference = first->name_token + 1;
        program->object_declarations[group->reference] = group->first;
        group->internal = check_input_linkage(input->declaration_bits, group->first) == 1;
        for (uint32_t e = 0; e < count; e += 1)
        {
            const uint32_t token = (uint32_t)events->keys[e];
            const uint32_t object = (uint32_t)(events->keys[e] >> 32);
            const CheckInputAttribute* attribute = check_cpu_dispatch_attribute(check_input_attributes_at(input, token));
            if (attribute == NULL || parser->tokens.names[token] != parser->tokens.names[group->first])
            {
                continue;
            }
            if (attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH)
            {
                group->dispatch = object;
                group->dispatch_declaration = group->dispatch_declaration == BUSTER_CHECK_NONE || token < group->dispatch_declaration ? token : group->dispatch_declaration;
                continue;
            }
            bool known = false;
            for (uint32_t v = group->version_first; v < version_count; v += 1)
            {
                known |= versions[v].object == object;
                versions[v].declaration = versions[v].object == object && token < versions[v].declaration ? token : versions[v].declaration;
            }
            if (!known)
            {
                versions[version_count++] = (CheckProgramCpuVersion){ .object = object, .declaration = token, .group = g };
            }
        }
        group->version_count = version_count - group->version_first;
    }
    if (valid)
    {
        program->cpu_groups = groups;
        program->cpu_group_count = group_count;
        program->cpu_versions = versions;
        program->cpu_version_count = version_count;
    }
    return valid;
}

// Whether the program has a body of `object`.
static bool check_program_cpu_defined(const CheckProgram* program, uint32_t object)
{
    bool defined = false;
    for (uint32_t b = 0; !defined && b < program->body_count; b += 1)
    {
        defined = program->bodies[b].object == object;
    }
    return defined;
}

// Before the symbols are numbered (check_program_alias_number): R is needed where a member of its group is, or the group
// has a dispatcher, whose object is needed for its own symbol (f, at the resolver); a version is needed for its own
// symbol where the program has its body, and its references move to R otherwise.
static void check_program_cpu_needed(const CheckProgram* program, uint8_t* needed)
{
    for (uint32_t g = 0; g < program->cpu_group_count; g += 1)
    {
        const CheckProgramCpuGroup* group = program->cpu_groups + g;
        bool any = group->dispatch != 0;
        for (uint32_t v = group->version_first; v < group->version_first + group->version_count; v += 1)
        {
            const uint32_t object = program->cpu_versions[v].object;
            any |= needed[object] != 0;
            needed[object] = check_program_cpu_defined(program, object);
        }
        needed[group->reference] |= any;
        if (group->dispatch != 0)
        {
            needed[group->dispatch] = 1;
        }
    }
}

// After the symbols are numbered: each version's and dispatcher's own symbol (CheckProgram.cpu_symbols, in `output`; 0
// for none), and every member's references at R's symbol. Returns false where there was no room.
static bool check_program_cpu_number(CheckProgram* program, Arena* output)
{
    const uint32_t count = program->cpu_version_count + program->cpu_group_count;
    SymbolId* symbols = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, count) : NULL;
    const bool valid = count == 0 || symbols != NULL;
    for (uint32_t g = 0; valid && g < program->cpu_group_count; g += 1)
    {
        const CheckProgramCpuGroup* group = program->cpu_groups + g;
        const SymbolId reference = program->object_symbols[group->reference];
        for (uint32_t v = group->version_first; v < group->version_first + group->version_count; v += 1)
        {
            const uint32_t object = program->cpu_versions[v].object;
            symbols[v] = program->object_symbols[object];
            program->object_symbols[object] = reference;
        }
        symbols[program->cpu_version_count + g] = group->dispatch != 0 ? program->object_symbols[group->dispatch] : 0;
        if (group->dispatch != 0)
        {
            program->object_symbols[group->dispatch] = reference;
        }
    }
    program->cpu_symbols = valid ? symbols : NULL;
    return valid;
}

// The symbol body `b` is defined at: its object's, but a version's own (CheckProgram.cpu_symbols).
static SymbolId check_program_body_symbol(const CheckProgram* program, uint32_t b)
{
    const uint32_t object = program->bodies[b].object;
    SymbolId symbol = program->object_symbols[object];
    for (uint32_t v = 0; program->cpu_symbols != NULL && v < program->cpu_version_count; v += 1)
    {
        symbol = program->cpu_versions[v].object == object ? program->cpu_symbols[v] : symbol;
    }
    return symbol;
}

// The features a version's body is compiled with beside the unit's (clang compiles a copy for each CPU of its list, each
// with that CPU's): those every CPU of its list has, comma-separated, in `output`, as a target attribute's text.
static String8 check_program_cpu_version_features(const CheckProgram* program, const CheckInputAttribute* attribute, Arena* output)
{
    int32_t rows[BUSTER_CHECK_PROGRAM_CPU_LIST];
    const uint32_t count = check_program_cpu_rows(program, attribute, rows, BUSTER_CHECK_PROGRAM_CPU_LIST);
    const char* first = count != 0 ? check_cpu_dispatch_rows[rows[0]].features : "";
    const uint64_t capacity = strlen(first) + 1;
    char* text = BUSTER_ARENA_PUSH_ARRAY(output, char, capacity);
    uint64_t length = 0;
    for (const char* item = first; text != NULL && *item != 0;)
    {
        const char* end = strchr(item, ',');
        const uint64_t item_length = end != NULL ? (uint64_t)(end - item) : strlen(item);
        bool everywhere = true;
        for (uint32_t r = 1; everywhere && r < count; r += 1)
        {
            bool found = false;
            for (const char* other = check_cpu_dispatch_rows[rows[r]].features; !found && *other != 0;)
            {
                const char* other_end = strchr(other, ',');
                const uint64_t other_length = other_end != NULL ? (uint64_t)(other_end - other) : strlen(other);
                found = other_length == item_length && memcmp(other, item, item_length) == 0;
                other = other_end != NULL ? other_end + 1 : other + other_length;
            }
            everywhere = found;
        }
        if (everywhere)
        {
            if (length != 0)
            {
                text[length++] = ',';
            }
            memcpy(text + length, item, item_length);
            length += item_length;
        }
        item = end != NULL ? end + 1 : item + item_length;
    }
    return (String8){ .pointer = text, .length = text != NULL ? length : 0 };
}

// A version's mangled name for the CPU of `row`: the function's name, a period and the CPU's mangling character (none
// where it has none, as clang's "f." then), in `output`.
static String8 check_program_cpu_name(const CheckProgram* program, uint32_t declaration, int32_t row, const char* suffix, Arena* output)
{
    const uint32_t identifier = program->parser->tokens.names[declaration];
    const uint32_t length = intern_length(program->identifiers, identifier);
    const uint64_t extra = suffix != NULL ? strlen(suffix) : (row >= 0 && check_cpu_dispatch_rows[row].mangling != 0 ? 2 : 1);
    char* text = BUSTER_ARENA_PUSH_ARRAY(output, char, (uint64_t)length + extra);
    if (text != NULL)
    {
        memcpy(text, intern_bytes(program->identifiers, identifier), length);
        if (suffix != NULL)
        {
            memcpy(text + length, suffix, extra);
        }
        else
        {
            text[length] = '.';
            if (extra == 2)
            {
                text[length + 1] = check_cpu_dispatch_rows[row].mangling;
            }
        }
    }
    return (String8){ .pointer = text, .length = text != NULL ? length + extra : 0 };
}

// The names of the groups' symbols before the emission adds them (`names` by symbol, CheckProgramCode's symbol_names):
// R's f.ifunc and each version's own f.X; a dispatcher's own keeps the function's name. Returns false where there was no
// room.
static bool check_program_cpu_names(const CheckProgram* program, String8* names, Arena* output)
{
    bool valid = true;
    for (uint32_t g = 0; valid && g < program->cpu_group_count; g += 1)
    {
        const CheckProgramCpuGroup* group = program->cpu_groups + g;
        const SymbolId reference = program->object_symbols[group->reference];
        if (reference != 0 && program->symbol_objects[reference - 1] == group->reference)
        {
            names[reference] = check_program_cpu_name(program, group->first, -1, ".ifunc", output);
            valid = names[reference].length != 0;
        }
        for (uint32_t v = group->version_first; valid && v < group->version_first + group->version_count; v += 1)
        {
            const SymbolId own = program->cpu_symbols != NULL ? program->cpu_symbols[v] : 0;
            int32_t rows[1];
            const CheckInputAttribute* attribute = check_cpu_dispatch_attribute(check_input_attributes_at(program->input, program->cpu_versions[v].declaration));
            if (own != 0 && attribute != NULL && check_program_cpu_rows(program, attribute, rows, 1) == 1)
            {
                names[own] = check_program_cpu_name(program, group->first, rows[0], NULL, output);
                valid = names[own].length != 0;
            }
        }
    }
    return valid;
}

// The link room the groups' code takes beyond the program's symbols (check_program_cpu_emit): the resolvers' atoms;
// their symbols and names (the resolver's, the CPU model's three and each CPU's version's) and the versions' other CPUs'
// names; and the resolvers' fixups.
static void check_program_cpu_room(const CheckProgram* program, uint64_t* atoms, uint64_t* symbols, uint64_t* fixups)
{
    for (uint32_t g = 0; g < program->cpu_group_count; g += 1)
    {
        const CheckProgramCpuGroup* group = program->cpu_groups + g;
        int32_t rows[BUSTER_CHECK_PROGRAM_CPU_LIST];
        const CheckInputAttribute* attribute = group->dispatch != 0 ? check_cpu_dispatch_attribute(check_input_attributes_at(program->input, group->dispatch_declaration)) : NULL;
        const uint32_t cpus = attribute != NULL ? check_program_cpu_rows(program, attribute, rows, BUSTER_CHECK_PROGRAM_CPU_LIST) : 0;
        *atoms += attribute != NULL;
        *symbols += attribute != NULL ? 4 + cpus : 0;
        *fixups += attribute != NULL ? 1 + 5 * (uint64_t)cpus : 0;
        for (uint32_t v = group->version_first; v < group->version_first + group->version_count; v += 1)
        {
            const CheckInputAttribute* version = check_cpu_dispatch_attribute(check_input_attributes_at(program->input, program->cpu_versions[v].declaration));
            const uint32_t listed = version != NULL ? check_program_cpu_rows(program, version, rows, BUSTER_CHECK_PROGRAM_CPU_LIST) : 0;
            *symbols += listed > 1 ? listed - 1 : 0;
        }
    }
}

// A resolver's option: a version's symbol and the mask of libgcc's CPU model its CPU's features give.
typedef struct CheckProgramCpuOption CheckProgramCpuOption;
struct CheckProgramCpuOption
{
    SymbolId symbol;
    uint32_t mask[4];
    String8 name;
};

// Whether mask `a` is below `b`, as std::array's comparison orders them: word 0 first.
static bool check_program_cpu_less(const uint32_t* a, const uint32_t* b)
{
    uint32_t w = 0;
    while (w < 3 && a[w] == b[w])
    {
        w += 1;
    }
    return a[w] < b[w];
}

// The symbol of the mangled `name` among the link's symbols from `first` on (the groups' own), or 0.
static SymbolId check_program_cpu_find(const Link* link, SymbolId first, String8 name)
{
    SymbolId found = 0;
    for (SymbolId s = first; found == 0 && s < link->symbols.count; s += 1)
    {
        const NameId id = link->symbols.name[s];
        found = id != 0 && link->names.length[id] == name.length && memcmp(link->names.bytes[id], name.pointer, name.length) == 0 ? s : 0;
    }
    return found;
}

// Appends `count` bytes to the resolver's code.
static void check_program_cpu_bytes(uint8_t* code, uint32_t* length, const uint8_t* bytes, uint32_t count)
{
    memcpy(code + *length, bytes, count);
    *length += count;
}

static void check_program_cpu_u32(uint8_t* code, uint32_t at, uint32_t value)
{
    memcpy(code + at, &value, 4);
}

// The symbol of the mangled `name`: a version's own (CheckProgram.cpu_symbols) or one of the link's from `first` on (the
// ones check_program_cpu_symbols added), or 0.
static SymbolId check_program_cpu_symbol(const Link* link, const CheckProgram* program, const CheckProgramCpuGroup* group, SymbolId first, String8 name)
{
    SymbolId symbol = 0;
    for (uint32_t v = group->version_first; symbol == 0 && v < group->version_first + group->version_count; v += 1)
    {
        const SymbolId own = program->cpu_symbols != NULL ? program->cpu_symbols[v] : 0;
        const NameId id = own != 0 && own < link->symbols.count ? link->symbols.name[own] : 0;
        symbol = id != 0 && link->names.length[id] == name.length && memcmp(link->names.bytes[id], name.pointer, name.length) == 0 ? own : 0;
    }
    return symbol != 0 ? symbol : check_program_cpu_find(link, first, name);
}

// A new symbol of `name` and `flags`, undefined: the program's where `program_defined` (the emission defines it).
static SymbolId check_program_cpu_add(Link* link, String8 name, uint8_t flags, bool program_defined)
{
    const NameId id = name.length != 0 ? link_add_name(link, (const uint8_t*)name.pointer, (uint32_t)name.length) : 0;
    const SymbolId symbol = id != 0 ? link_add_symbol(link, id, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) : 0;
    if (symbol != 0 && program_defined)
    {
        link->symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
    }
    return symbol;
}

// The groups' symbols beyond the program's, once the program's, the helpers', the runtime's and the templates' exist and
// before an executable's runtime joins (bind.c): each version's other CPUs' names, of its own symbol's flags; and for
// each group with a dispatcher, its resolver f.resolver (a weak function, local in a static group), each of its CPUs'
// versions the unit doesn't define (undefined globals), and f.ifunc and f made IFUNCs of the resolver's binding. Names in
// `output`. Returns false with `code`'s status and reason set.
static bool check_program_cpu_symbols(CheckProgramCode* code, const CheckProgram* program, Arena* output)
{
    Link* link = &code->link;
    bool valid = true;
    code->cpu_first = link->symbols.count;
    for (uint32_t g = 0; valid && g < program->cpu_group_count; g += 1)
    {
        const CheckProgramCpuGroup* group = program->cpu_groups + g;
        int32_t rows[BUSTER_CHECK_PROGRAM_CPU_LIST];
        for (uint32_t v = group->version_first; valid && v < group->version_first + group->version_count; v += 1)
        {
            const SymbolId own = program->cpu_symbols != NULL ? program->cpu_symbols[v] : 0;
            const CheckInputAttribute* attribute = check_cpu_dispatch_attribute(check_input_attributes_at(program->input, program->cpu_versions[v].declaration));
            const uint32_t listed = own != 0 && attribute != NULL ? check_program_cpu_rows(program, attribute, rows, BUSTER_CHECK_PROGRAM_CPU_LIST) : 0;
            for (uint32_t r = 1; valid && r < listed; r += 1)
            {
                valid = check_program_cpu_add(link, check_program_cpu_name(program, group->first, rows[r], NULL, output), link->symbols.flags[own], true) != 0;
            }
        }
        const CheckInputAttribute* dispatch = group->dispatch != 0 ? check_cpu_dispatch_attribute(check_input_attributes_at(program->input, group->dispatch_declaration)) : NULL;
        const SymbolId reference = program->object_symbols[group->reference];
        const SymbolId alias = program->cpu_symbols != NULL ? program->cpu_symbols[program->cpu_version_count + g] : 0;
        if (!valid || dispatch == NULL || reference == 0 || alias == 0)
        {
            continue;
        }
        const uint8_t binding = group->internal ? BUSTER_LINK_SYMBOL_LOCAL : BUSTER_LINK_SYMBOL_WEAK;
        valid = check_program_cpu_add(link, check_program_cpu_name(program, group->first, -1, ".resolver", output), binding | BUSTER_LINK_SYMBOL_FUNCTION, true) != 0;
        const uint32_t cpus = check_program_cpu_rows(program, dispatch, rows, BUSTER_CHECK_PROGRAM_CPU_LIST);
        for (uint32_t c = 0; valid && c < cpus; c += 1)
        {
            const String8 name = check_program_cpu_name(program, group->first, rows[c], NULL, output);
            valid = name.length != 0 && (check_program_cpu_symbol(link, program, group, code->cpu_first, name) != 0 ||
                                         check_program_cpu_add(link, name, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, false) != 0);
        }
        const SymbolId ifuncs[2] = { reference, alias };
        for (uint32_t i = 0; valid && i < 2; i += 1)
        {
            link->symbols.flags[ifuncs[i]] = (uint8_t)((link->symbols.flags[ifuncs[i]] & BUSTER_LINK_SYMBOL_VISIBILITY) | binding | BUSTER_LINK_SYMBOL_IFUNC);
            link->symbols.source[ifuncs[i]] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    if (!valid)
    {
        code->status = CHECK_PROGRAM_RESOURCE;
        code->reason = "a cpu_specific or cpu_dispatch function's symbols couldn't be added";
    }
    return valid;
}

// The groups' code once the functions' atoms exist (after check_program_cpu_symbols): each version's other CPUs' names
// at its body; and for each group with a dispatcher, its resolver's atom, in `bytes`, with f.resolver, f.ifunc and f at
// it. The resolver is clang's EmitX86MultiVersionResolver in other instructions:
//
//     push rax                                  ; the stack aligned for the call
//     call __cpu_indicator_init                 ; PLT32
//   for each option, its mask's nonzero words w (0: [__cpu_model + 12], 1 to 3: [__cpu_features2 + 4 * (w - 1)]):
//     mov eax, [rip + word]                     ; PC32
//     and eax, mask
//     cmp eax, mask
//     jne next
//     lea rax, [rip + f.X]                      ; PC32 to a version the unit defines,
//     (mov rax, [rip + f.X@GOTPCREL])           ; REX_GOTPCRELX to one it doesn't
//     pop rcx
//     ret
//   next: ...
//     ud2                                       ; no option's features: clang's trap
//
// The options are the dispatcher's CPUs, each its version's symbol, sorted by their masks, the most first (stable, as
// clang's llvm::stable_sort of getCpuSupportsMask's arrays), with all but one of several trailing options of no mask
// dropped, as clang drops the least advanced (the lower name). An option of no mask is taken unconditionally. The CPU
// model's symbols are the lowering's (check_lower_finish makes them for a unit with a dispatcher). Returns false with
// `code`'s status and reason set.
static bool check_program_cpu_atoms(CheckProgramCode* code, const CheckProgram* program, Arena* bytes)
{
    Link* link = &code->link;
    bool valid = true;
    for (uint32_t g = 0; valid && g < program->cpu_group_count; g += 1)
    {
        const CheckProgramCpuGroup* group = program->cpu_groups + g;
        int32_t rows[BUSTER_CHECK_PROGRAM_CPU_LIST];
        // Each version's other CPUs' names, at its body.
        for (uint32_t v = group->version_first; valid && v < group->version_first + group->version_count; v += 1)
        {
            const SymbolId own = program->cpu_symbols != NULL ? program->cpu_symbols[v] : 0;
            const CheckInputAttribute* attribute = check_cpu_dispatch_attribute(check_input_attributes_at(program->input, program->cpu_versions[v].declaration));
            const uint32_t listed = own != 0 && attribute != NULL ? check_program_cpu_rows(program, attribute, rows, BUSTER_CHECK_PROGRAM_CPU_LIST) : 0;
            const AtomId atom = own != 0 && own < link->symbols.count ? link->symbols.atom[own] : 0;
            for (uint32_t r = 1; valid && atom != 0 && r < listed; r += 1)
            {
                const SymbolId symbol = check_program_cpu_find(link, code->cpu_first, check_program_cpu_name(program, group->first, rows[r], NULL, bytes));
                valid = symbol != 0 && link_define_symbol(link, symbol, atom, link->symbols.value[own], link->symbols.size[own]);
            }
        }
        const CheckInputAttribute* dispatch = group->dispatch != 0 ? check_cpu_dispatch_attribute(check_input_attributes_at(program->input, group->dispatch_declaration)) : NULL;
        const SymbolId reference = program->object_symbols[group->reference];
        const SymbolId alias = program->cpu_symbols != NULL ? program->cpu_symbols[program->cpu_version_count + g] : 0;
        if (!valid || dispatch == NULL || reference == 0 || alias == 0)
        {
            continue;
        }
        const uint32_t cpus = check_program_cpu_rows(program, dispatch, rows, BUSTER_CHECK_PROGRAM_CPU_LIST);
        CheckProgramCpuOption options[BUSTER_CHECK_PROGRAM_CPU_LIST];
        uint32_t option_count = 0;
        for (uint32_t c = 0; valid && c < cpus; c += 1)
        {
            const String8 name = check_program_cpu_name(program, group->first, rows[c], NULL, bytes);
            CheckProgramCpuOption option = { .symbol = name.length != 0 ? check_program_cpu_symbol(link, program, group, code->cpu_first, name) : 0, .name = name };
            valid = option.symbol != 0;
            memcpy(option.mask, check_cpu_dispatch_rows[rows[c]].mask, sizeof(option.mask));
            // Stable insertion, the greatest mask first.
            uint32_t at = option_count;
            while (at > 0 && check_program_cpu_less(options[at - 1].mask, option.mask))
            {
                options[at] = options[at - 1];
                at -= 1;
            }
            options[at] = option;
            option_count += 1;
        }
        // Several trailing options of no mask: clang keeps the one of the greater name.
        while (option_count > 1 && (options[option_count - 2].mask[0] | options[option_count - 2].mask[1] | options[option_count - 2].mask[2] |
                                    options[option_count - 2].mask[3]) == 0)
        {
            const String8 left = options[option_count - 2].name;
            const String8 right = options[option_count - 1].name;
            const uint64_t shorter = left.length < right.length ? left.length : right.length;
            const int compared = memcmp(left.pointer, right.pointer, shorter);
            if (compared < 0 || (compared == 0 && left.length < right.length))
            {
                options[option_count - 2] = options[option_count - 1];
            }
            option_count -= 1;
        }
        const SymbolId indicator = code->lowered.runtime_symbols[CHECK_LOWER_RUNTIME_CPU_INDICATOR_INIT];
        const SymbolId model = code->lowered.runtime_symbols[CHECK_LOWER_RUNTIME_CPU_MODEL];
        const SymbolId features = code->lowered.runtime_symbols[CHECK_LOWER_RUNTIME_CPU_FEATURES2];
        const String8 resolver_name = check_program_cpu_name(program, group->first, -1, ".resolver", bytes);
        const SymbolId resolver = resolver_name.length != 0 ? check_program_cpu_find(link, code->cpu_first, resolver_name) : 0;
        valid = valid && indicator != 0 && model != 0 && features != 0 && resolver != 0;
        // The code: 6 bytes for the entry, 22 for each mask word and 9 for each option's return, and 2.
        const uint32_t capacity = 6 + option_count * (4 * 22 + 9) + 2;
        uint8_t* text = valid ? arena_push(bytes, capacity, 16) : NULL;
        valid = text != NULL;
        // The fixups, in offset order.
        uint32_t fixup_offsets[1 + 5 * BUSTER_CHECK_PROGRAM_CPU_LIST];
        uint8_t fixup_kinds[1 + 5 * BUSTER_CHECK_PROGRAM_CPU_LIST];
        SymbolId fixup_targets[1 + 5 * BUSTER_CHECK_PROGRAM_CPU_LIST];
        int32_t fixup_addends[1 + 5 * BUSTER_CHECK_PROGRAM_CPU_LIST];
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
        }
        bool taken = false;
        for (uint32_t o = 0; valid && !taken && o < option_count; o += 1)
        {
            const CheckProgramCpuOption* option = options + o;
            // The jne's displacements to patch at the next option.
            uint32_t branches[4];
            uint32_t branch_count = 0;
            for (uint32_t w = 0; w < 4; w += 1)
            {
                if (option->mask[w] != 0)
                {
                    static const uint8_t test[] = { 0x8b, 0x05, 0, 0, 0, 0, 0x25, 0, 0, 0, 0, 0x3d, 0, 0, 0, 0, 0x0f, 0x85, 0, 0, 0, 0 };
                    check_program_cpu_bytes(text, &length, test, sizeof(test));
                    fixup_offsets[fixups] = length - 20;
                    fixup_kinds[fixups] = BUSTER_LINK_FIXUP_PC32;
                    fixup_targets[fixups] = w == 0 ? model : features;
                    fixup_addends[fixups] = (w == 0 ? 12 : 4 * ((int32_t)w - 1)) - 4;
                    fixups += 1;
                    check_program_cpu_u32(text, length - 15, option->mask[w]);
                    check_program_cpu_u32(text, length - 10, option->mask[w]);
                    branches[branch_count++] = length - 4;
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
            for (uint32_t b = 0; b < branch_count; b += 1)
            {
                check_program_cpu_u32(text, branches[b], length - (branches[b] + 4));
            }
            // An option of no mask is taken: none after it is reached (clang's default must be last).
            taken = branch_count == 0;
        }
        if (valid && !taken)
        {
            static const uint8_t trap[] = { 0x0f, 0x0b };
            check_program_cpu_bytes(text, &length, trap, sizeof(trap));
        }
        const AtomId atom = valid ? link_add_atom(link, BUSTER_LINK_CLASS_TEXT, 4, length, text, 0) : 0;
        valid = atom != 0;
        for (uint32_t f = 0; valid && f < fixups; f += 1)
        {
            valid = link_add_fixup(link, atom, fixup_offsets[f], fixup_kinds[f], fixup_targets[f], fixup_addends[f]) != 0;
        }
        const SymbolId defined[3] = { resolver, reference, alias };
        for (uint32_t d = 0; valid && d < 3; d += 1)
        {
            valid = link_define_symbol(link, defined[d], atom, 0, length);
        }
    }
    if (!valid)
    {
        code->status = CHECK_PROGRAM_BACKEND;
        code->reason = "a cpu_dispatch function's resolver couldn't be emitted";
    }
    return valid;
}

#endif
