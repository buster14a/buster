// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Transactional Link data materialization for the interpreter. Include after interpreter.c and link.c.
// Inputs, their names and atom bytes are borrowed and immutable. Output arrays, copied atoms and memory regions
// belong to arena; they live until its caller releases them. The arena's available range is disjoint from all input
// arrays, bytes and the output record. Every indexed table includes the unused id zero.
// Symbol ids remain local to each input Link. Global spellings resolve across inputs with strong-over-weak and
// first-weak rules; local symbols and unnamed aliases retain their input identity. Explicit symbol bindings supply
// native addresses or the interpreter's opaque function tokens. Code is never copied or executed here.
//
// Each data atom becomes one memory region, so aliases neither widen its bounds nor remove its write protection.
// BSS is explicitly cleared even when arena storage is reused; all static bytes are defined. Read-only atoms are
// patched privately before publication and remain read-only to the interpreter. A failed call preserves output,
// all inputs and arena length. New allocations above the restored length have no observable lifetime on failure.
// The producer emits contiguous address/size columns and compact bindings for both interpreter front ends. This
// scalar oracle resolves diagnostic-sized symbol tables; no SIMD or end-to-end throughput claim is made.
#ifndef BUSTER_INTERPRETER_LINK_INCLUDED
#define BUSTER_INTERPRETER_LINK_INCLUDED

typedef enum InterpreterLinkStatus
{
    INTERPRETER_LINK_OK,
    INTERPRETER_LINK_INVALID,
    INTERPRETER_LINK_RESOURCE,
    INTERPRETER_LINK_DUPLICATE,
    INTERPRETER_LINK_UNDEFINED,
    INTERPRETER_LINK_UNSUPPORTED,
    INTERPRETER_LINK_RANGE,
} InterpreterLinkStatus;

typedef struct InterpreterLinkResult InterpreterLinkResult;
struct InterpreterLinkResult
{
    InterpreterLinkStatus status;
    uint32_t input;
    SymbolId symbol;
    AtomId atom;
    FixupId fixup;
};

typedef struct InterpreterLinkExternal InterpreterLinkExternal;
struct InterpreterLinkExternal
{
    uint32_t input;
    SymbolId symbol;
    uintptr_t address;
    uint64_t size;
    bool writable;
};

typedef struct InterpreterLinkUnit InterpreterLinkUnit;
struct InterpreterLinkUnit
{
    uintptr_t* addresses;
    uint64_t* sizes;
    uint8_t* resolved;
    uint8_t* writable;
    // Whether the selected definition denotes registered data, independently of a reference's ELF type.
    uint8_t* data;
    uint8_t** atom_bytes;
    uint32_t symbol_count;
    uint32_t atom_count;
    // Compact data-symbol aliases only; callable tokens are only in addresses, never memory registrations.
    InterpreterBinding* bindings;
    uint32_t binding_count;
};

typedef struct InterpreterLink InterpreterLink;
struct InterpreterLink
{
    InterpreterLinkUnit* units;
    uint32_t unit_count;
    InterpreterMemory* memory;
    uint32_t memory_count;
};

static __attribute__((unused)) InterpreterLinkResult interpreter_link_materialize(InterpreterLink* output, const Link* const* links, uint32_t link_count, const InterpreterLinkExternal* externals, uint32_t external_count, Arena* arena);

static InterpreterLinkResult interpreter_link_problem(InterpreterLinkStatus status, uint32_t input, SymbolId symbol, AtomId atom, FixupId fixup)
{
    const InterpreterLinkResult result = {
        .status = status,
        .input = input,
        .symbol = symbol,
        .atom = atom,
        .fixup = fixup,
    };
    return result;
}

static bool interpreter_link_code(uint8_t class)
{
    const bool result = class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI;
    return result;
}

static bool interpreter_link_writable(uint8_t class)
{
    const bool result = class == BUSTER_LINK_CLASS_DATA || class == BUSTER_LINK_CLASS_BSS;
    return result;
}

static bool interpreter_link_data_kind(uint8_t kind)
{
    const bool result = kind == BUSTER_LINK_FIXUP_ABS64 || kind == BUSTER_LINK_FIXUP_PC64 || kind == BUSTER_LINK_FIXUP_PC32 ||
                        kind == BUSTER_LINK_FIXUP_ABS32 || kind == BUSTER_LINK_FIXUP_ABS32S || kind == BUSTER_LINK_FIXUP_SIZE32 || kind == BUSTER_LINK_FIXUP_SIZE64;
    return result;
}

// Validate bounded columns before any helper indexes them. Unlike link_check, unresolved declarations are legal:
// this consumer supplies name resolution without writing any of the Link's linker-owned columns or error field.
static InterpreterLinkResult interpreter_link_validate(const Link* link, uint32_t input, Arena* arena)
{
    InterpreterLinkResult result = interpreter_link_problem(INTERPRETER_LINK_OK, input, 0, 0, 0);
    const uint64_t before = arena->length;
    if (link == 0 || link->error != LINK_ERROR_NONE || link->names.count == 0 || link->atoms.count == 0 || link->symbols.count == 0 || link->fixups.count == 0 ||
        link->names.count > link->names.capacity || link->atoms.count > link->atoms.capacity || link->symbols.count > link->symbols.capacity || link->fixups.count > link->fixups.capacity ||
        link->fixups.wide_count == 0 || link->fixups.wide_count > link->fixups.wide_capacity || link->priorities.count == 0 || link->priorities.count > link->priorities.capacity ||
        link->names.bytes == 0 || link->names.length == 0 || link->atoms.class == 0 || link->atoms.alignment_log2 == 0 || link->atoms.size == 0 || link->atoms.bytes == 0 ||
        link->atoms.unit == 0 || link->atoms.fixup_begin == 0 || link->atoms.fixup_count == 0 || link->symbols.name == 0 || link->symbols.atom == 0 || link->symbols.value == 0 ||
        link->symbols.size == 0 || link->symbols.flags == 0 || link->symbols.source == 0 || link->fixups.offset == 0 || link->fixups.target == 0 || link->fixups.kind == 0 || link->fixups.addend == 0)
    {
        result.status = INTERPRETER_LINK_INVALID;
    }
    uint64_t* owned = 0;
    if (result.status == INTERPRETER_LINK_OK)
    {
        const uint64_t words = link->fixups.count / 64ull + 1;
        owned = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words);
        if (owned == 0)
        {
            result.status = INTERPRETER_LINK_RESOURCE;
        }
        else
        {
            memset(owned, 0, words * sizeof(*owned));
        }
    }
    for (NameId name = 1; result.status == INTERPRETER_LINK_OK && name < link->names.count; name += 1)
    {
        if (link->names.length[name] == 0 || link->names.bytes[name] == 0)
        {
            result.status = INTERPRETER_LINK_INVALID;
        }
    }
    for (uint32_t entry = 1; result.status == INTERPRETER_LINK_OK && entry < link->priorities.count; entry += 1)
    {
        if (link->priorities.atom == 0 || link->priorities.priority == 0)
        {
            result.status = INTERPRETER_LINK_INVALID;
        }
        else
        {
            const AtomId atom = link->priorities.atom[entry];
            if (atom == 0 || atom >= link->atoms.count || !link_class_is_array(link->atoms.class[atom]) ||
                (entry > 1 && link->priorities.atom[entry - 1] >= atom))
            {
                result = interpreter_link_problem(INTERPRETER_LINK_INVALID, input, 0, atom, 0);
            }
        }
    }
    for (SymbolId symbol = 1; result.status == INTERPRETER_LINK_OK && symbol < link->symbols.count; symbol += 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        const uint8_t source = link->symbols.source[symbol];
        const uint8_t flags = link->symbols.flags[symbol];
        const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
        const bool atom_source = source == BUSTER_LINK_SOURCE_PROGRAM || source == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE || source == BUSTER_LINK_SOURCE_FOREIGN_OBJECT;
        const bool local = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
        if (link->symbols.name[symbol] >= link->names.count || atom >= link->atoms.count || source >= BUSTER_LINK_SOURCE_COUNT || !link_symbol_flags_are_valid(flags) ||
            (atom != 0 && (!atom_source || !link_symbol_fits(link, atom, link->symbols.value[symbol], link->symbols.size[symbol], flags))) ||
            (source == BUSTER_LINK_SOURCE_ABSOLUTE && !link_absolute_fits(atom, flags)) || (local && atom == 0 && source != BUSTER_LINK_SOURCE_ABSOLUTE))
        {
            result = interpreter_link_problem(INTERPRETER_LINK_INVALID, input, symbol, atom, 0);
        }
        else if (type == BUSTER_LINK_SYMBOL_TLS || type == BUSTER_LINK_SYMBOL_IFUNC)
        {
            result = interpreter_link_problem(INTERPRETER_LINK_UNSUPPORTED, input, symbol, atom, 0);
        }
    }
    uint64_t fixup_count = 0;
    for (AtomId atom = 1; result.status == INTERPRETER_LINK_OK && atom < link->atoms.count; atom += 1)
    {
        const uint8_t class = link->atoms.class[atom];
        const bool bytes_valid = link_class_is_zero_fill(class) ? link->atoms.bytes[atom] == 0 : link->atoms.size[atom] == 0 || link->atoms.bytes[atom] != 0;
        if (class == BUSTER_LINK_CLASS_NONE || class >= BUSTER_LINK_CLASS_COUNT || link->atoms.alignment_log2[atom] > BUSTER_LINK_MAX_ALIGNMENT_LOG2 || !bytes_valid ||
            !link_check_fixups(link, atom, owned))
        {
            result = interpreter_link_problem(INTERPRETER_LINK_INVALID, input, 0, atom, 0);
        }
        else if (link_class_is_tls(class))
        {
            result = interpreter_link_problem(INTERPRETER_LINK_UNSUPPORTED, input, 0, atom, 0);
        }
        fixup_count += link->atoms.fixup_count[atom];
        const FixupId begin = link->atoms.fixup_begin[atom];
        for (uint32_t i = 0; result.status == INTERPRETER_LINK_OK && i < link->atoms.fixup_count[atom]; i += 1)
        {
            const FixupId fixup = begin + i;
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const SymbolId target = link->fixups.target[fixup];
            const AtomId target_atom = link->symbols.atom[target];
            const bool local = (link->symbols.flags[target] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
            if ((link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0 && link->fixups.wide_addends == 0)
            {
                result = interpreter_link_problem(INTERPRETER_LINK_INVALID, input, target, atom, fixup);
            }
            else if (local && target_atom != 0 && link->atoms.unit[target_atom] != link->atoms.unit[atom])
            {
                result = interpreter_link_problem(INTERPRETER_LINK_INVALID, input, target, atom, fixup);
            }
            else if (interpreter_link_code(class) || !interpreter_link_data_kind(kind))
            {
                result = interpreter_link_problem(INTERPRETER_LINK_UNSUPPORTED, input, target, atom, fixup);
            }
        }
    }
    if (result.status == INTERPRETER_LINK_OK && fixup_count + 1 != link->fixups.count)
    {
        result.status = INTERPRETER_LINK_INVALID;
    }
    arena_pop_to(arena, before);
    return result;
}

static const InterpreterLinkExternal* interpreter_link_external(const InterpreterLinkExternal* externals, uint32_t count, uint32_t input, SymbolId symbol)
{
    const InterpreterLinkExternal* result = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (externals[i].input == input && externals[i].symbol == symbol)
        {
            result = &externals[i];
        }
    }
    return result;
}

static bool interpreter_link_same_name(const Link* a, SymbolId x, const Link* b, SymbolId y)
{
    const NameId first = a->symbols.name[x];
    const NameId second = b->symbols.name[y];
    const bool result = first != 0 && second != 0 && a->names.length[first] == b->names.length[second] &&
                        memcmp(a->names.bytes[first], b->names.bytes[second], a->names.length[first]) == 0;
    return result;
}

static bool interpreter_link_definition(const Link* link, SymbolId symbol, const InterpreterLinkExternal* external)
{
    const bool result = link->symbols.atom[symbol] != 0 || link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE || external != 0;
    return result;
}

static InterpreterLinkResult interpreter_link_resolve(InterpreterLink* image, const Link* const* links, uint32_t count, const InterpreterLinkExternal* externals, uint32_t external_count)
{
    InterpreterLinkResult result = interpreter_link_problem(INTERPRETER_LINK_OK, 0, 0, 0, 0);
    for (uint32_t input = 0; result.status == INTERPRETER_LINK_OK && input < count; input += 1)
    {
        const Link* link = links[input];
        InterpreterLinkUnit* unit = &image->units[input];
        for (SymbolId symbol = 1; result.status == INTERPRETER_LINK_OK && symbol < link->symbols.count; symbol += 1)
        {
            const uint8_t binding = link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING;
            uint32_t selected_input = input;
            SymbolId selected_symbol = symbol;
            bool found = false;
            bool strong = false;
            if (binding == BUSTER_LINK_SYMBOL_LOCAL || link->symbols.name[symbol] == 0)
            {
                found = interpreter_link_definition(link, symbol, interpreter_link_external(externals, external_count, input, symbol));
            }
            else
            {
                for (uint32_t candidate_input = 0; result.status == INTERPRETER_LINK_OK && candidate_input < count; candidate_input += 1)
                {
                    const Link* candidate = links[candidate_input];
                    for (SymbolId candidate_symbol = 1; result.status == INTERPRETER_LINK_OK && candidate_symbol < candidate->symbols.count; candidate_symbol += 1)
                    {
                        const uint8_t candidate_binding = candidate->symbols.flags[candidate_symbol] & BUSTER_LINK_SYMBOL_BINDING;
                        const InterpreterLinkExternal* external = interpreter_link_external(externals, external_count, candidate_input, candidate_symbol);
                        if (candidate_binding != BUSTER_LINK_SYMBOL_LOCAL && interpreter_link_definition(candidate, candidate_symbol, external) &&
                            interpreter_link_same_name(link, symbol, candidate, candidate_symbol))
                        {
                            const bool candidate_strong = candidate_binding == BUSTER_LINK_SYMBOL_GLOBAL;
                            const Link* selected = links[selected_input];
                            const bool same_definition = found && candidate_input == selected_input && candidate->symbols.atom[candidate_symbol] != 0 &&
                                                         candidate->symbols.atom[candidate_symbol] == selected->symbols.atom[selected_symbol] &&
                                                         candidate->symbols.value[candidate_symbol] == selected->symbols.value[selected_symbol];
                            if (found && strong && candidate_strong && !same_definition)
                            {
                                result = interpreter_link_problem(INTERPRETER_LINK_DUPLICATE, candidate_input, candidate_symbol, candidate->symbols.atom[candidate_symbol], 0);
                            }
                            else if (!found || (!strong && candidate_strong))
                            {
                                selected_input = candidate_input;
                                selected_symbol = candidate_symbol;
                                found = true;
                                strong = candidate_strong;
                            }
                        }
                    }
                }
            }
            if (result.status == INTERPRETER_LINK_OK && found)
            {
                const InterpreterLinkUnit* selected = &image->units[selected_input];
                unit->addresses[symbol] = selected->addresses[selected_symbol];
                unit->sizes[symbol] = selected->sizes[selected_symbol];
                unit->resolved[symbol] = selected->resolved[selected_symbol];
                unit->writable[symbol] = selected->writable[selected_symbol];
                unit->data[symbol] = selected->data[selected_symbol];
            }
            else if (result.status == INTERPRETER_LINK_OK && binding == BUSTER_LINK_SYMBOL_WEAK)
            {
                unit->resolved[symbol] = 1;
            }
            else if (result.status == INTERPRETER_LINK_OK && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_FUNCTION)
            {
                result = interpreter_link_problem(INTERPRETER_LINK_UNDEFINED, input, symbol, 0, 0);
            }
        }
    }
    return result;
}

static InterpreterLinkResult interpreter_link_patch(InterpreterLink* image, const Link* const* links, uint32_t count)
{
    InterpreterLinkResult result = interpreter_link_problem(INTERPRETER_LINK_OK, 0, 0, 0, 0);
    for (uint32_t input = 0; result.status == INTERPRETER_LINK_OK && input < count; input += 1)
    {
        const Link* link = links[input];
        const InterpreterLinkUnit* unit = &image->units[input];
        for (AtomId atom = 1; result.status == INTERPRETER_LINK_OK && atom < link->atoms.count; atom += 1)
        {
            const FixupId begin = link->atoms.fixup_begin[atom];
            for (uint32_t i = 0; result.status == INTERPRETER_LINK_OK && i < link->atoms.fixup_count[atom]; i += 1)
            {
                const FixupId fixup = begin + i;
                const SymbolId symbol = link->fixups.target[fixup];
                const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
                const bool size = kind == BUSTER_LINK_FIXUP_SIZE32 || kind == BUSTER_LINK_FIXUP_SIZE64;
                const uint8_t width = link_fixup_kinds[kind].width;
                uint8_t* field = unit->atom_bytes[atom] + link->fixups.offset[fixup];
                // The existing executable patcher computes ELF's 64-bit modular arithmetic too. Narrow fields
                // then require the appropriate zero/sign extension, including negative absolute symbols.
                uint64_t bits = (size ? unit->sizes[symbol] : unit->addresses[symbol]) + (uint64_t)link_fixup_addend(link, fixup);
                if (link_fixup_kinds[kind].pc_relative)
                {
                    bits -= (uintptr_t)field;
                }
                if (!size && unit->resolved[symbol] == 0)
                {
                    result = interpreter_link_problem(INTERPRETER_LINK_UNDEFINED, input, symbol, atom, fixup);
                }
                else
                {
                    bool fits = width == 8;
                    if (kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_ABS32S)
                    {
                        fits = (int64_t)bits == (int32_t)bits;
                    }
                    else if (width == 4)
                    {
                        fits = bits <= UINT32_MAX;
                    }
                    if (!fits)
                    {
                        result = interpreter_link_problem(INTERPRETER_LINK_RANGE, input, symbol, atom, fixup);
                    }
                    else
                    {
                        for (uint32_t byte = 0; byte < width; byte += 1)
                        {
                            field[byte] = (uint8_t)(bits >> (byte * 8));
                        }
                    }
                }
            }
        }
    }
    return result;
}

static InterpreterLinkResult interpreter_link_materialize(InterpreterLink* output, const Link* const* links, uint32_t link_count, const InterpreterLinkExternal* externals, uint32_t external_count, Arena* arena)
{
    InterpreterLinkResult result = interpreter_link_problem(INTERPRETER_LINK_OK, 0, 0, 0, 0);
    const uint64_t before = arena != 0 ? arena->length : 0;
    InterpreterLink image = { 0 };
    if (output == 0 || arena == 0 || (link_count != 0 && links == 0) || (external_count != 0 && externals == 0))
    {
        result.status = INTERPRETER_LINK_INVALID;
    }
    uint64_t memory_capacity = external_count;
    for (uint32_t input = 0; result.status == INTERPRETER_LINK_OK && input < link_count; input += 1)
    {
        result = interpreter_link_validate(links[input], input, arena);
        if (result.status == INTERPRETER_LINK_OK)
        {
            memory_capacity += links[input]->atoms.count - 1ull;
            if (memory_capacity > UINT32_MAX)
            {
                result.status = INTERPRETER_LINK_RESOURCE;
            }
        }
    }
    for (uint32_t i = 0; result.status == INTERPRETER_LINK_OK && i < external_count; i += 1)
    {
        const InterpreterLinkExternal* external = &externals[i];
        if (external->input >= link_count || external->symbol == 0 || external->symbol >= links[external->input]->symbols.count ||
            external->size > UINTPTR_MAX - external->address || (external->size != 0 && external->address == 0))
        {
            result = interpreter_link_problem(INTERPRETER_LINK_INVALID, external->input, external->symbol, 0, 0);
        }
        else
        {
            const Link* link = links[external->input];
            const AtomId atom = link->symbols.atom[external->symbol];
            if ((atom != 0 && !interpreter_link_code(link->atoms.class[atom])) || link->symbols.source[external->symbol] == BUSTER_LINK_SOURCE_ABSOLUTE)
            {
                result = interpreter_link_problem(INTERPRETER_LINK_INVALID, external->input, external->symbol, atom, 0);
            }
        }
        for (uint32_t j = 0; result.status == INTERPRETER_LINK_OK && j < i; j += 1)
        {
            if (externals[j].input == external->input && externals[j].symbol == external->symbol)
            {
                result = interpreter_link_problem(INTERPRETER_LINK_DUPLICATE, external->input, external->symbol, 0, 0);
            }
        }
    }
    if (result.status == INTERPRETER_LINK_OK && link_count != 0)
    {
        image.units = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterLinkUnit, link_count);
        image.memory = memory_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterMemory, memory_capacity) : 0;
        if (image.units == 0 || (memory_capacity != 0 && image.memory == 0))
        {
            result.status = INTERPRETER_LINK_RESOURCE;
        }
        else
        {
            memset(image.units, 0, link_count * sizeof(*image.units));
            image.unit_count = link_count;
        }
    }
    for (uint32_t input = 0; result.status == INTERPRETER_LINK_OK && input < link_count; input += 1)
    {
        const Link* link = links[input];
        InterpreterLinkUnit* unit = &image.units[input];
        unit->symbol_count = link->symbols.count;
        unit->atom_count = link->atoms.count;
        unit->addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uintptr_t, unit->symbol_count);
        unit->sizes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, unit->symbol_count);
        unit->resolved = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, unit->symbol_count);
        unit->writable = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, unit->symbol_count);
        unit->data = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, unit->symbol_count);
        unit->atom_bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t*, unit->atom_count);
        unit->bindings = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterBinding, unit->symbol_count);
        if (unit->addresses == 0 || unit->sizes == 0 || unit->resolved == 0 || unit->writable == 0 || unit->data == 0 || unit->atom_bytes == 0 || unit->bindings == 0)
        {
            result = interpreter_link_problem(INTERPRETER_LINK_RESOURCE, input, 0, 0, 0);
        }
        else
        {
            memset(unit->addresses, 0, unit->symbol_count * sizeof(*unit->addresses));
            memset(unit->sizes, 0, unit->symbol_count * sizeof(*unit->sizes));
            memset(unit->resolved, 0, unit->symbol_count);
            memset(unit->writable, 0, unit->symbol_count);
            memset(unit->data, 0, unit->symbol_count);
            memset(unit->atom_bytes, 0, unit->atom_count * sizeof(*unit->atom_bytes));
        }
        for (AtomId atom = 1; result.status == INTERPRETER_LINK_OK && atom < link->atoms.count; atom += 1)
        {
            const uint8_t class = link->atoms.class[atom];
            if (!interpreter_link_code(class))
            {
                const uint64_t size = link->atoms.size[atom];
                uint8_t* bytes = arena_push(arena, size != 0 ? size : 1, 1ull << link->atoms.alignment_log2[atom]);
                if (bytes == 0)
                {
                    result = interpreter_link_problem(INTERPRETER_LINK_RESOURCE, input, 0, atom, 0);
                }
                else
                {
                    unit->atom_bytes[atom] = bytes;
                    if (class == BUSTER_LINK_CLASS_BSS)
                    {
                        memset(bytes, 0, size);
                    }
                    else if (size != 0)
                    {
                        memcpy(bytes, link->atoms.bytes[atom], size);
                    }
                    image.memory[image.memory_count++] = (InterpreterMemory){
                        .bytes = bytes,
                        .size = size,
                        .writable = interpreter_link_writable(class),
                    };
                }
            }
        }
        for (SymbolId symbol = 1; result.status == INTERPRETER_LINK_OK && symbol < link->symbols.count; symbol += 1)
        {
            const AtomId atom = link->symbols.atom[symbol];
            const InterpreterLinkExternal* external = interpreter_link_external(externals, external_count, input, symbol);
            unit->sizes[symbol] = link->symbols.size[symbol];
            if (external != 0)
            {
                unit->addresses[symbol] = external->address;
                unit->sizes[symbol] = external->size;
                unit->writable[symbol] = external->writable;
                unit->resolved[symbol] = 1;
                if ((link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_FUNCTION && (atom == 0 || !interpreter_link_code(link->atoms.class[atom])))
                {
                    unit->data[symbol] = 1;
                    image.memory[image.memory_count++] = (InterpreterMemory){
                        .bytes = (uint8_t*)external->address,
                        .size = external->size,
                        .writable = external->writable,
                    };
                }
            }
            else if (atom != 0 && unit->atom_bytes[atom] != 0)
            {
                unit->addresses[symbol] = (uintptr_t)(unit->atom_bytes[atom] + link->symbols.value[symbol]);
                unit->writable[symbol] = interpreter_link_writable(link->atoms.class[atom]);
                unit->resolved[symbol] = 1;
                unit->data[symbol] = 1;
            }
            else if (link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE)
            {
                unit->addresses[symbol] = link->symbols.value[symbol];
                unit->resolved[symbol] = 1;
            }
        }
    }
    if (result.status == INTERPRETER_LINK_OK)
    {
        result = interpreter_link_resolve(&image, links, link_count, externals, external_count);
    }
    if (result.status == INTERPRETER_LINK_OK)
    {
        result = interpreter_link_patch(&image, links, link_count);
    }
    for (uint32_t input = 0; result.status == INTERPRETER_LINK_OK && input < link_count; input += 1)
    {
        const Link* link = links[input];
        InterpreterLinkUnit* unit = &image.units[input];
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            if (unit->data[symbol] != 0 && unit->resolved[symbol] != 0)
            {
                unit->bindings[unit->binding_count++] = (InterpreterBinding){
                    .symbol = symbol,
                    .bytes = (uint8_t*)unit->addresses[symbol],
                    .size = unit->sizes[symbol],
                    .writable = unit->writable[symbol] != 0,
                };
            }
        }
    }
    if (result.status == INTERPRETER_LINK_OK)
    {
        *output = image;
    }
    else if (arena != 0)
    {
        arena_pop_to(arena, before);
    }
    return result;
}

#endif
