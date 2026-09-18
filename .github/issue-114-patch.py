from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def replace_regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex match, found {count}")
    return result


def patch_object_h() -> None:
    path = "src/buster/lib/compiler/object/object.h"
    text = read(path)
    old = '''// A replaceable definition: COFF selectany COMDAT (IMAGE_SCN_LNK_COMDAT with
// a selection other than NODUPLICATES), ELF STB_WEAK, or Mach-O N_WEAK_DEF.
// The linker keeps the first such definition instead of diagnosing a
// duplicate, and any non-replaceable definition of the same name wins over
// every replaceable one regardless of input order.
typedef struct ObjectSymbol ObjectSymbol;
struct ObjectSymbol
{
    String8 name;
    u64 value;
    u64 size;
    u32 section;
    ObjectSymbolKind kind;
    bool global;
    bool weak;
    // Not exported from the final image: ELF STV_HIDDEN.  A `.hidden`
    // directive in module-level assembly is the only producer today, and only
    // the ELF writer and reader carry it -- COFF and Mach-O have no
    // equivalent per-symbol visibility byte.
    bool hidden;
    u8 reserved;
};

typedef struct ObjectRelocation ObjectRelocation;
struct ObjectRelocation
{
    s64 addend;
    u64 offset;
    u32 section;
    u32 symbol;
    ObjectRelocationKind kind;
};
'''
    new = '''// COFF gives every COMDAT source section one of these selection contracts.
// The numeric values deliberately match IMAGE_COMDAT_SELECT_* so the reader
// can validate and preserve the auxiliary section-definition byte directly.
typedef enum ObjectComdatSelection
{
    OBJECT_COMDAT_SELECTION_NONE,
    OBJECT_COMDAT_SELECTION_NO_DUPLICATES,
    OBJECT_COMDAT_SELECTION_ANY,
    OBJECT_COMDAT_SELECTION_SAME_SIZE,
    OBJECT_COMDAT_SELECTION_EXACT_MATCH,
    OBJECT_COMDAT_SELECTION_ASSOCIATIVE,
    OBJECT_COMDAT_SELECTION_LARGEST,
    OBJECT_COMDAT_SELECTION_COUNT,
} ObjectComdatSelection;

#define OBJECT_COMDAT_ASSOCIATED_NONE UINT32_MAX

// One original COFF COMDAT section contribution after the reader has merged
// sections of the same neutral kind. `offset`/`size` identify its bytes in
// `sections[section]`; `source_section` preserves the input section number;
// and `associated` is another record in this array for ASSOCIATIVE sections.
// Relocations stay contiguous per source section and are recorded explicitly
// so EXACT_MATCH can compare their canonical identity without rescanning the
// entire object. A record with selection NONE represents a malformed/pending
// COMDAT and deliberately keeps the old hard-definition behavior.
typedef struct ObjectComdat ObjectComdat;
struct ObjectComdat
{
    String8 key;
    u64 offset;
    u64 size;
    u32 section;
    u32 source_section;
    u32 associated;
    u32 first_relocation;
    u32 relocation_count;
    ObjectComdatSelection selection;
};

// A replaceable definition: a selected COFF COMDAT contribution, ELF
// STB_WEAK, or Mach-O N_WEAK_DEF. COFF selection is resolved as a group before
// this ordinary weak/strong arbitration; `comdat` is zero for every other
// symbol and otherwise names ObjectFile.comdats[comdat - 1].
typedef struct ObjectSymbol ObjectSymbol;
struct ObjectSymbol
{
    String8 name;
    u64 value;
    u64 size;
    u32 section;
    u32 comdat;
    ObjectSymbolKind kind;
    bool global;
    bool weak;
    // Not exported from the final image: ELF STV_HIDDEN.  A `.hidden`
    // directive in module-level assembly is the only producer today, and only
    // the ELF writer and reader carry it -- COFF and Mach-O have no
    // equivalent per-symbol visibility byte.
    bool hidden;
    u8 reserved;
};

typedef struct ObjectRelocation ObjectRelocation;
struct ObjectRelocation
{
    s64 addend;
    u64 offset;
    u32 section;
    u32 symbol;
    // Zero outside a COFF COMDAT; otherwise ObjectFile.comdats[comdat - 1].
    // The field fills the structure's former tail padding on 64-bit hosts.
    u32 comdat;
    ObjectRelocationKind kind;
};
'''
    text = replace_once(text, old, new, "object.h COMDAT model")
    text = replace_once(
        text,
        '''    ObjectSection* sections;
    ObjectSymbol* symbols;
    ObjectRelocation* relocations;
    Target target;
''',
        '''    ObjectSection* sections;
    ObjectSymbol* symbols;
    ObjectRelocation* relocations;
    ObjectComdat* comdats;
    Target target;
''',
        "object.h ObjectFile pointers",
    )
    text = replace_once(
        text,
        '''    u32 section_count;
    u32 symbol_count;
    u32 relocation_count;
    ObjectDebugModule* debug_modules;
''',
        '''    u32 section_count;
    u32 symbol_count;
    u32 relocation_count;
    u32 comdat_count;
    ObjectDebugModule* debug_modules;
''',
        "object.h ObjectFile counts",
    )
    write(path, text)


def patch_object_c() -> None:
    path = "src/buster/lib/compiler/object/object.c"
    text = read(path)
    text = replace_once(
        text,
        '''// The three formats spell "this definition may be dropped for another one"
// differently — COFF selectany COMDAT, ELF STB_WEAK, Mach-O N_WEAK_DEF — and
// all three read into ObjectSymbol.weak, which link_objects arbitrates on.
// ELF and Mach-O write it back out; COFF cannot, because a COMDAT needs its
// own section and this model merges sections by kind.
''',
        '''// The three formats spell "this definition may be dropped for another one"
// differently. ELF STB_WEAK and Mach-O N_WEAK_DEF read into ObjectSymbol.weak.
// COFF first preserves each source section's COMDAT key, selection, parent,
// byte range and relocation range in ObjectFile.comdats; link_objects resolves
// those groups before ordinary weak/strong arbitration. ELF and Mach-O write
// weak definitions back out. COFF still cannot synthesize them because a
// COMDAT needs its own section and the producer model merges sections by kind.
''',
        "object.c orientation",
    )
    old_enum = '''enum
{
    OBJECT_COFF_SECTION_LINK_COMDAT = 0x00001000,
    OBJECT_COFF_STORAGE_EXTERNAL = 2,
    OBJECT_COFF_STORAGE_STATIC = 3,
    OBJECT_COFF_STORAGE_WEAK_EXTERNAL = 105,
    OBJECT_COFF_COMDAT_NONE = 0,
    OBJECT_COFF_COMDAT_NO_DUPLICATES = 1,
    OBJECT_COFF_COMDAT_ANY = 2,
    OBJECT_COFF_COMDAT_SAME_SIZE = 3,
    OBJECT_COFF_COMDAT_EXACT_MATCH = 4,
    OBJECT_COFF_COMDAT_ASSOCIATIVE = 5,
    OBJECT_COFF_COMDAT_LARGEST = 6,
    OBJECT_COFF_COMDAT_PENDING = 0xff,
};

// Every selection but NODUPLICATES lets the linker keep one definition and
// drop the rest, so all of them become replaceable definitions.  SAME_SIZE
// and EXACT_MATCH are not verified and LARGEST takes the first definition
// rather than the biggest one: sections are merged by kind here, so no
// per-COMDAT identity survives into the linker to compare or re-pick with.
BUSTER_GLOBAL_LOCAL bool object_coff_comdat_is_replaceable(u8 selection)
{
    return selection >= OBJECT_COFF_COMDAT_ANY && selection <= OBJECT_COFF_COMDAT_LARGEST;
}
'''
    new_enum = '''enum
{
    OBJECT_COFF_SECTION_LINK_COMDAT = 0x00001000,
    OBJECT_COFF_STORAGE_EXTERNAL = 2,
    OBJECT_COFF_STORAGE_STATIC = 3,
    OBJECT_COFF_STORAGE_WEAK_EXTERNAL = 105,
    OBJECT_COFF_COMDAT_PENDING = 0xff,
};

// Ordinary symbol arbitration still needs to know that a selected COMDAT
// definition yields to a strong non-COMDAT definition. The dedicated COMDAT
// phase decides which contribution survives before that arbitration runs.
BUSTER_GLOBAL_LOCAL bool object_coff_comdat_is_replaceable(u8 selection)
{
    return selection >= OBJECT_COMDAT_SELECTION_ANY && selection <= OBJECT_COMDAT_SELECTION_LARGEST;
}
'''
    text = replace_once(text, old_enum, new_enum, "object.c COMDAT enum")
    start = text.index("BUSTER_GLOBAL_LOCAL ObjectFile object_read_coff(")
    end = text.index("BUSTER_GLOBAL_LOCAL ObjectFile object_read_mach_o64", start)
    prefix, region, suffix = text[:start], text[start:end], text[end:]
    region = region.replace("OBJECT_COFF_COMDAT_NONE", "OBJECT_COMDAT_SELECTION_NONE")
    region = region.replace("OBJECT_COFF_COMDAT_NO_DUPLICATES", "OBJECT_COMDAT_SELECTION_NO_DUPLICATES")
    region = region.replace("OBJECT_COFF_COMDAT_ANY", "OBJECT_COMDAT_SELECTION_ANY")
    region = region.replace("OBJECT_COFF_COMDAT_SAME_SIZE", "OBJECT_COMDAT_SELECTION_SAME_SIZE")
    region = region.replace("OBJECT_COFF_COMDAT_EXACT_MATCH", "OBJECT_COMDAT_SELECTION_EXACT_MATCH")
    region = region.replace("OBJECT_COFF_COMDAT_ASSOCIATIVE", "OBJECT_COMDAT_SELECTION_ASSOCIATIVE")
    region = region.replace("OBJECT_COFF_COMDAT_LARGEST", "OBJECT_COMDAT_SELECTION_LARGEST")
    region = replace_once(
        region,
        '''    u8* section_comdat_selections = 0;
    if (read_ok)
    {
        section_comdat_selections = arena_allocate(arena, u8, section_count);
    }
    if (read_ok)
    {
        if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(ObjectInitializerSection), BUSTER_ALIGN_OF(ObjectInitializerSection)))
''',
        '''    u8* section_comdat_selections = 0;
    if (read_ok)
    {
        section_comdat_selections = arena_allocate(arena, u8, section_count);
        if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(u32), BUSTER_ALIGN_OF(u32)))
        {
            read_ok = false;
        }
    }
    u32* section_comdat_indices = 0;
    if (read_ok)
    {
        section_comdat_indices = arena_allocate(arena, u32, section_count);
        memset(section_comdat_indices, 0xff, (u64)section_count * sizeof(*section_comdat_indices));
        if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(u16), BUSTER_ALIGN_OF(u16)))
        {
            read_ok = false;
        }
    }
    u16* section_comdat_associations = 0;
    if (read_ok)
    {
        section_comdat_associations = arena_allocate(arena, u16, section_count);
        memset(section_comdat_associations, 0, (u64)section_count * sizeof(*section_comdat_associations));
        if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(ObjectComdat), BUSTER_ALIGN_OF(ObjectComdat)))
        {
            read_ok = false;
        }
    }
    if (read_ok)
    {
        result.comdats = arena_allocate(arena, ObjectComdat, section_count);
    }
    if (read_ok)
    {
        if (!object_reader_arena_can_allocate_count(arena, section_count, sizeof(ObjectInitializerSection), BUSTER_ALIGN_OF(ObjectInitializerSection)))
''',
        "object.c COMDAT allocations",
    )
    region = replace_once(
        region,
        '''            if (read_ok)
            {
                section_kinds[section_index] = (u32)kind;
                section_bases[section_index] = base;
                section_prefix_sizes[section_index] = prefix_size;
''',
        '''            if (read_ok && section_comdat_selections[section_index] == OBJECT_COFF_COMDAT_PENDING)
            {
                u32 comdat_index = result.comdat_count++;
                section_comdat_indices[section_index] = comdat_index;
                result.comdats[comdat_index] = (ObjectComdat){
                    .offset = base,
                    .size = raw_size - prefix_size,
                    .section = (u32)kind,
                    .source_section = section_index,
                    .associated = OBJECT_COMDAT_ASSOCIATED_NONE,
                    .selection = OBJECT_COMDAT_SELECTION_NONE,
                };
            }
            if (read_ok)
            {
                section_kinds[section_index] = (u32)kind;
                section_bases[section_index] = base;
                section_prefix_sizes[section_index] = prefix_size;
''',
        "object.c COMDAT contribution creation",
    )
    region = replace_once(
        region,
        '''                if (section_number > 0 && storage == OBJECT_COFF_STORAGE_STATIC && auxiliary_count &&
                    section_comdat_selections[section_number - 1] == OBJECT_COFF_COMDAT_PENDING)
                {
                    section_comdat_selections[section_number - 1] = bytes.pointer[source + COFF_SYMBOL_SIZE + 14];
                }
''',
        '''                if (section_number > 0 && storage == OBJECT_COFF_STORAGE_STATIC && auxiliary_count &&
                    section_comdat_selections[section_number - 1] == OBJECT_COFF_COMDAT_PENDING)
                {
                    u16 section_index = (u16)section_number - 1;
                    u8 selection = bytes.pointer[source + COFF_SYMBOL_SIZE + 14];
                    if (selection >= OBJECT_COMDAT_SELECTION_NO_DUPLICATES && selection < OBJECT_COMDAT_SELECTION_COUNT)
                    {
                        section_comdat_selections[section_index] = selection;
                        u32 comdat_index = section_comdat_indices[section_index];
                        if (comdat_index == UINT32_MAX)
                        {
                            read_ok = false;
                        }
                        else
                        {
                            result.comdats[comdat_index].selection = (ObjectComdatSelection)selection;
                            if (selection == OBJECT_COMDAT_SELECTION_ASSOCIATIVE &&
                                !object_read_u16(bytes, source + COFF_SYMBOL_SIZE + 12, &section_comdat_associations[section_index]))
                            {
                                read_ok = false;
                            }
                        }
                    }
                }
''',
        "object.c COMDAT auxiliary parsing",
    )
    old_symbol = '''                        result.symbols[destination_index] = (ObjectSymbol){
                            .name = string_duplicate_arena(arena, name, false),
                            .value = section_number ? symbol_base + symbol_value : 0,
                            .section = section_number ? section_kinds[section_index] : OBJECT_SECTION_UNDEFINED,
                            .kind = symbol_type & 0x20 ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
                            .global = storage == OBJECT_COFF_STORAGE_EXTERNAL || storage == OBJECT_COFF_STORAGE_WEAK_EXTERNAL,
                            .weak = section_number != 0 && object_coff_comdat_is_replaceable(section_comdat_selections[section_index]),
                        };
                        symbol_map[source_index] = destination_index;
'''
    new_symbol = '''                        u32 comdat_index = section_number ? section_comdat_indices[section_index] : UINT32_MAX;
                        result.symbols[destination_index] = (ObjectSymbol){
                            .name = string_duplicate_arena(arena, name, false),
                            .value = section_number ? symbol_base + symbol_value : 0,
                            .section = section_number ? section_kinds[section_index] : OBJECT_SECTION_UNDEFINED,
                            .comdat = comdat_index == UINT32_MAX ? 0 : comdat_index + 1,
                            .kind = symbol_type & 0x20 ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
                            .global = storage == OBJECT_COFF_STORAGE_EXTERNAL || storage == OBJECT_COFF_STORAGE_WEAK_EXTERNAL,
                            .weak = section_number != 0 && object_coff_comdat_is_replaceable(section_comdat_selections[section_index]),
                        };
                        if (comdat_index != UINT32_MAX && result.symbols[destination_index].global &&
                            result.comdats[comdat_index].selection != OBJECT_COMDAT_SELECTION_ASSOCIATIVE &&
                            !result.comdats[comdat_index].key.length)
                        {
                            result.comdats[comdat_index].key = result.symbols[destination_index].name;
                        }
                        symbol_map[source_index] = destination_index;
'''
    region = replace_once(region, old_symbol, new_symbol, "object.c symbol COMDAT identity")
    relocation_allocation = '''    if (read_ok)
    {
        if (!object_reader_arena_can_allocate_count(arena, relocation_capacity, sizeof(ObjectRelocation), BUSTER_ALIGN_OF(ObjectRelocation)))
'''
    association_validation = '''    if (read_ok)
    {
        for (u32 comdat_index = 0; comdat_index < result.comdat_count && read_ok; comdat_index += 1)
        {
            ObjectComdat* comdat = result.comdats + comdat_index;
            if (comdat->selection == OBJECT_COMDAT_SELECTION_ASSOCIATIVE)
            {
                u16 associated_section = section_comdat_associations[comdat->source_section];
                if (!associated_section || associated_section > section_count ||
                    section_comdat_indices[associated_section - 1] == UINT32_MAX ||
                    section_comdat_indices[associated_section - 1] == comdat_index)
                {
                    read_ok = false;
                }
                else
                {
                    comdat->associated = section_comdat_indices[associated_section - 1];
                }
            }
            else if (comdat->selection != OBJECT_COMDAT_SELECTION_NONE && !comdat->key.length)
            {
                // A COMDAT without an external leader cannot participate in
                // key arbitration. Preserve the previous hard-definition
                // behavior instead of silently discarding it.
                comdat->selection = OBJECT_COMDAT_SELECTION_NONE;
            }
        }
    }
'''
    region = replace_once(region, relocation_allocation, association_validation + relocation_allocation, "object.c association validation")
    region = replace_once(
        region,
        '''            if (section_kinds[section_index] == UINT32_MAX)
            {
                continue;
            }
            for (u16 relocation_index = 0; relocation_index < relocation_count && read_ok; relocation_index += 1)
''',
        '''            if (section_kinds[section_index] == UINT32_MAX)
            {
                continue;
            }
            u32 source_comdat_index = section_comdat_indices[section_index];
            if (source_comdat_index != UINT32_MAX)
            {
                result.comdats[source_comdat_index].first_relocation = result.relocation_count;
            }
            for (u16 relocation_index = 0; relocation_index < relocation_count && read_ok; relocation_index += 1)
''',
        "object.c relocation contribution start",
    )
    pattern = re.compile(
        r'''result\.relocations\[result\.relocation_count\+\+\] = \(ObjectRelocation\)\{\n\s*\.addend = addend,\n\s*\.offset = section_bases\[section_index\] \+ source_offset,\n\s*\.section = section_kinds\[section_index\],\n\s*\.symbol = symbol_map\[source_symbol\],\n\s*\.kind = kind,\n\s*\};'''
    )
    replacement = '''u32 destination_relocation = result.relocation_count++;
                        result.relocations[destination_relocation] = (ObjectRelocation){
                            .addend = addend,
                            .offset = section_bases[section_index] + source_offset,
                            .section = section_kinds[section_index],
                            .symbol = symbol_map[source_symbol],
                            .comdat = source_comdat_index == UINT32_MAX ? 0 : source_comdat_index + 1,
                            .kind = kind,
                        };
                        if (source_comdat_index != UINT32_MAX)
                        {
                            result.comdats[source_comdat_index].relocation_count += 1;
                        }'''
    region, count = pattern.subn(replacement, region, count=1)
    if count != 1:
        raise RuntimeError(f"object.c relocation identity: expected one match, found {count}")
    text = prefix + region + suffix
    write(path, text)


def patch_link_h() -> None:
    path = "src/buster/lib/compiler/link/link.h"
    text = read(path)
    text = replace_once(
        text,
        '''    LINK_ERROR_TARGET_MISMATCH,
    LINK_ERROR_DUPLICATE_SYMBOL,
    LINK_ERROR_UNRESOLVED_SYMBOL,
''',
        '''    LINK_ERROR_TARGET_MISMATCH,
    LINK_ERROR_DUPLICATE_SYMBOL,
    LINK_ERROR_COMDAT_SELECTION_MISMATCH,
    LINK_ERROR_COMDAT_SIZE_MISMATCH,
    LINK_ERROR_COMDAT_EXACT_MATCH,
    LINK_ERROR_UNRESOLVED_SYMBOL,
''',
        "link.h COMDAT errors",
    )
    write(path, text)


COMDAT_LINK_CODE = r'''
typedef enum LinkComdatState
{
    LINK_COMDAT_STATE_UNKNOWN,
    LINK_COMDAT_STATE_KEEP,
    LINK_COMDAT_STATE_DISCARD,
} LinkComdatState;

typedef struct LinkComdatPlan LinkComdatPlan;
struct LinkComdatPlan
{
    u64* object_offsets;
    u8* states;
    u64 count;
};

typedef struct LinkComdatGroup LinkComdatGroup;
struct LinkComdatGroup
{
    String8 key;
    u32 object_index;
    u32 comdat_index;
    ObjectComdatSelection selection;
};

typedef struct LinkComdatTable LinkComdatTable;
struct LinkComdatTable
{
    LinkComdatGroup* groups;
    u32* slots;
    u64 capacity;
    u32 count;
};

BUSTER_GLOBAL_LOCAL bool link_comdat_table_initialize(Arena* arena, u64 count, LinkComdatTable* table)
{
    bool result = table != 0;
    if (result)
    {
        *table = (LinkComdatTable){0};
        if (count)
        {
            result = count <= UINT64_MAX / 2;
            u64 capacity = 1;
            u64 required = result ? count * 2 : 0;
            while (result && capacity < required)
            {
                result = capacity <= UINT64_MAX / 2;
                if (result)
                {
                    capacity *= 2;
                }
            }
            result = result && capacity <= UINT64_MAX / sizeof(*table->slots) && count <= UINT64_MAX / sizeof(*table->groups);
            if (result)
            {
                table->groups = arena_allocate(arena, LinkComdatGroup, count);
                table->slots = arena_allocate(arena, u32, capacity);
                table->capacity = capacity;
                memset(table->slots, 0xff, capacity * sizeof(*table->slots));
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32* link_comdat_table_slot(LinkComdatTable* table, String8 key)
{
    u32* result = 0;
    if (table && table->capacity)
    {
        u64 mask = table->capacity - 1;
        u64 slot_index = buster_hash_64((u8*)key.pointer, key.length) & mask;
        for (u64 probe = 0; !result && probe < table->capacity; probe += 1)
        {
            u32* slot = table->slots + slot_index;
            if (*slot == UINT32_MAX || string_equal(table->groups[*slot].key, key))
            {
                result = slot;
            }
            slot_index = (slot_index + 1) & mask;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s32 link_comdat_string_compare(String8 left, String8 right)
{
    u64 length = BUSTER_MIN(left.length, right.length);
    s32 result = length ? (s32)memcmp(left.pointer, right.pointer, length) : 0;
    if (!result && left.length != right.length)
    {
        result = left.length < right.length ? -1 : 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 link_comdat_byte(ObjectFile* object, ObjectComdat* comdat, u64 index)
{
    ObjectSection* section = object->sections + comdat->section;
    u64 offset = comdat->offset + index;
    return offset < section->data.length ? section->data.pointer[offset] : 0;
}

BUSTER_GLOBAL_LOCAL bool link_comdat_symbol_inside(ObjectSymbol* symbol, ObjectComdat* comdat)
{
    bool result = symbol->section == comdat->section && symbol->value >= comdat->offset;
    if (result)
    {
        u64 relative = symbol->value - comdat->offset;
        result = relative <= comdat->size;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s32 link_comdat_symbol_identity_compare(ObjectFile* left_object, ObjectComdat* left_comdat, u32 left_index,
                                                            ObjectFile* right_object, ObjectComdat* right_comdat, u32 right_index)
{
    s32 result = 0;
    if (left_index >= left_object->symbol_count || right_index >= right_object->symbol_count)
    {
        result = left_index < right_index ? -1 : left_index != right_index;
    }
    else
    {
        ObjectSymbol* left = left_object->symbols + left_index;
        ObjectSymbol* right = right_object->symbols + right_index;
        bool left_inside = !left->global && left->section != OBJECT_SECTION_UNDEFINED && link_comdat_symbol_inside(left, left_comdat);
        bool right_inside = !right->global && right->section != OBJECT_SECTION_UNDEFINED && link_comdat_symbol_inside(right, right_comdat);
        if (left_inside && right_inside)
        {
            u64 left_relative = left->value - left_comdat->offset;
            u64 right_relative = right->value - right_comdat->offset;
            if (left->kind != right->kind)
            {
                result = left->kind < right->kind ? -1 : 1;
            }
            else if (left_relative != right_relative)
            {
                result = left_relative < right_relative ? -1 : 1;
            }
            else if (left->size != right->size)
            {
                result = left->size < right->size ? -1 : 1;
            }
        }
        else
        {
            result = link_comdat_string_compare(left->name, right->name);
            if (!result && left->global != right->global)
            {
                result = left->global ? 1 : -1;
            }
            if (!result && left->kind != right->kind)
            {
                result = left->kind < right->kind ? -1 : 1;
            }
            bool left_defined = left->section != OBJECT_SECTION_UNDEFINED;
            bool right_defined = right->section != OBJECT_SECTION_UNDEFINED;
            if (!result && left_defined != right_defined)
            {
                result = left_defined ? 1 : -1;
            }
            if (!result && left_defined)
            {
                ObjectSectionKind left_kind = left_object->sections[left->section].kind;
                ObjectSectionKind right_kind = right_object->sections[right->section].kind;
                if (left_kind != right_kind)
                {
                    result = left_kind < right_kind ? -1 : 1;
                }
                else if (left->value != right->value)
                {
                    result = left->value < right->value ? -1 : 1;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s32 link_comdat_identity_compare(ObjectFile* left_object, ObjectComdat* left,
                                                     ObjectFile* right_object, ObjectComdat* right)
{
    s32 result = 0;
    u64 shared_size = BUSTER_MIN(left->size, right->size);
    ObjectSection* left_section = left_object->sections + left->section;
    ObjectSection* right_section = right_object->sections + right->section;
    if (left->offset <= left_section->data.length && right->offset <= right_section->data.length &&
        shared_size <= left_section->data.length - left->offset && shared_size <= right_section->data.length - right->offset)
    {
        result = shared_size ? (s32)memcmp(left_section->data.pointer + left->offset, right_section->data.pointer + right->offset, shared_size) : 0;
    }
    else
    {
        for (u64 index = 0; !result && index < shared_size; index += 1)
        {
            u8 left_byte = link_comdat_byte(left_object, left, index);
            u8 right_byte = link_comdat_byte(right_object, right, index);
            if (left_byte != right_byte)
            {
                result = left_byte < right_byte ? -1 : 1;
            }
        }
    }
    if (!result && left->size != right->size)
    {
        result = left->size < right->size ? -1 : 1;
    }
    u32 shared_relocations = BUSTER_MIN(left->relocation_count, right->relocation_count);
    for (u32 index = 0; !result && index < shared_relocations; index += 1)
    {
        ObjectRelocation* left_relocation = left_object->relocations + left->first_relocation + index;
        ObjectRelocation* right_relocation = right_object->relocations + right->first_relocation + index;
        u64 left_offset = left_relocation->offset - left->offset;
        u64 right_offset = right_relocation->offset - right->offset;
        if (left_offset != right_offset)
        {
            result = left_offset < right_offset ? -1 : 1;
        }
        else if (left_relocation->kind != right_relocation->kind)
        {
            result = left_relocation->kind < right_relocation->kind ? -1 : 1;
        }
        else if (left_relocation->addend != right_relocation->addend)
        {
            result = left_relocation->addend < right_relocation->addend ? -1 : 1;
        }
        else
        {
            result = link_comdat_symbol_identity_compare(left_object, left, left_relocation->symbol,
                                                         right_object, right, right_relocation->symbol);
        }
    }
    if (!result && left->relocation_count != right->relocation_count)
    {
        result = left->relocation_count < right->relocation_count ? -1 : 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool link_comdat_record_valid(ObjectFile* object, u32 index)
{
    bool result = object && index < object->comdat_count;
    if (result)
    {
        ObjectComdat* comdat = object->comdats + index;
        result = comdat->selection < OBJECT_COMDAT_SELECTION_COUNT && comdat->section < object->section_count;
        if (result)
        {
            ObjectSection* section = object->sections + comdat->section;
            u64 size = BUSTER_MAX(section->data.length, section->virtual_size);
            result = comdat->offset <= size && comdat->size <= size - comdat->offset &&
                     comdat->first_relocation <= object->relocation_count &&
                     comdat->relocation_count <= object->relocation_count - comdat->first_relocation;
        }
        if (result && comdat->selection != OBJECT_COMDAT_SELECTION_NONE &&
            comdat->selection != OBJECT_COMDAT_SELECTION_ASSOCIATIVE)
        {
            result = comdat->key.length && comdat->key.pointer;
        }
        if (result && comdat->selection == OBJECT_COMDAT_SELECTION_ASSOCIATIVE)
        {
            result = comdat->associated < object->comdat_count && comdat->associated != index;
        }
        for (u32 relocation_index = 0; result && relocation_index < comdat->relocation_count; relocation_index += 1)
        {
            ObjectRelocation* relocation = object->relocations + comdat->first_relocation + relocation_index;
            result = relocation->comdat == index + 1 && relocation->section == comdat->section &&
                     relocation->offset >= comdat->offset && relocation->offset - comdat->offset < comdat->size &&
                     relocation->symbol < object->symbol_count;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL LinkError link_comdat_plan_build(Arena* arena, ObjectFile* objects, u32 object_count,
                                                     LinkComdatPlan* plan, String8* error_symbol)
{
    LinkError error = LINK_ERROR_NONE;
    *plan = (LinkComdatPlan){0};
    *error_symbol = (String8){0};
    plan->object_offsets = arena_allocate(arena, u64, (u64)object_count + 1);
    for (u32 object_index = 0; object_index < object_count && error == LINK_ERROR_NONE; object_index += 1)
    {
        plan->object_offsets[object_index] = plan->count;
        if (objects[object_index].comdat_count > UINT64_MAX - plan->count)
        {
            error = LINK_ERROR_INVALID_INPUT;
        }
        else
        {
            plan->count += objects[object_index].comdat_count;
        }
    }
    plan->object_offsets[object_count] = plan->count;
    if (error == LINK_ERROR_NONE)
    {
        plan->states = arena_allocate(arena, u8, plan->count ? plan->count : 1);
        memset(plan->states, LINK_COMDAT_STATE_UNKNOWN, plan->count);
        LinkComdatTable table = {0};
        if (!link_comdat_table_initialize(arena, plan->count, &table))
        {
            error = LINK_ERROR_INVALID_INPUT;
        }
        for (u32 object_index = 0; object_index < object_count && error == LINK_ERROR_NONE; object_index += 1)
        {
            ObjectFile* object = objects + object_index;
            u64 base = plan->object_offsets[object_index];
            for (u32 comdat_index = 0; comdat_index < object->comdat_count && error == LINK_ERROR_NONE; comdat_index += 1)
            {
                ObjectComdat* comdat = object->comdats + comdat_index;
                u64 state_index = base + comdat_index;
                if (!link_comdat_record_valid(object, comdat_index))
                {
                    error = LINK_ERROR_INVALID_INPUT;
                }
                else if (comdat->selection == OBJECT_COMDAT_SELECTION_NONE)
                {
                    plan->states[state_index] = LINK_COMDAT_STATE_KEEP;
                }
                else if (comdat->selection != OBJECT_COMDAT_SELECTION_ASSOCIATIVE)
                {
                    u32* slot = link_comdat_table_slot(&table, comdat->key);
                    if (!slot)
                    {
                        error = LINK_ERROR_INVALID_INPUT;
                    }
                    else if (*slot == UINT32_MAX)
                    {
                        u32 group_index = table.count++;
                        *slot = group_index;
                        table.groups[group_index] = (LinkComdatGroup){
                            .key = comdat->key,
                            .object_index = object_index,
                            .comdat_index = comdat_index,
                            .selection = comdat->selection,
                        };
                        plan->states[state_index] = LINK_COMDAT_STATE_KEEP;
                    }
                    else
                    {
                        LinkComdatGroup* group = table.groups + *slot;
                        ObjectFile* winner_object = objects + group->object_index;
                        ObjectComdat* winner = winner_object->comdats + group->comdat_index;
                        u64 winner_state = plan->object_offsets[group->object_index] + group->comdat_index;
                        *error_symbol = comdat->key;
                        if (group->selection != comdat->selection)
                        {
                            error = LINK_ERROR_COMDAT_SELECTION_MISMATCH;
                        }
                        else if (comdat->selection == OBJECT_COMDAT_SELECTION_NO_DUPLICATES)
                        {
                            error = LINK_ERROR_DUPLICATE_SYMBOL;
                        }
                        else if (comdat->selection == OBJECT_COMDAT_SELECTION_SAME_SIZE && winner->size != comdat->size)
                        {
                            error = LINK_ERROR_COMDAT_SIZE_MISMATCH;
                        }
                        else if (comdat->selection == OBJECT_COMDAT_SELECTION_EXACT_MATCH &&
                                 link_comdat_identity_compare(winner_object, winner, object, comdat) != 0)
                        {
                            error = LINK_ERROR_COMDAT_EXACT_MATCH;
                        }
                        else
                        {
                            bool replace = comdat->selection == OBJECT_COMDAT_SELECTION_LARGEST &&
                                           (comdat->size > winner->size ||
                                            (comdat->size == winner->size &&
                                             link_comdat_identity_compare(object, comdat, winner_object, winner) > 0));
                            if (replace)
                            {
                                plan->states[winner_state] = LINK_COMDAT_STATE_DISCARD;
                                plan->states[state_index] = LINK_COMDAT_STATE_KEEP;
                                group->object_index = object_index;
                                group->comdat_index = comdat_index;
                            }
                            else
                            {
                                plan->states[state_index] = LINK_COMDAT_STATE_DISCARD;
                            }
                        }
                    }
                }
            }
        }
        bool progress = true;
        for (u64 pass = 0; error == LINK_ERROR_NONE && progress && pass < plan->count; pass += 1)
        {
            progress = false;
            for (u32 object_index = 0; object_index < object_count; object_index += 1)
            {
                ObjectFile* object = objects + object_index;
                u64 base = plan->object_offsets[object_index];
                for (u32 comdat_index = 0; comdat_index < object->comdat_count; comdat_index += 1)
                {
                    ObjectComdat* comdat = object->comdats + comdat_index;
                    u64 state_index = base + comdat_index;
                    if (comdat->selection == OBJECT_COMDAT_SELECTION_ASSOCIATIVE &&
                        plan->states[state_index] == LINK_COMDAT_STATE_UNKNOWN)
                    {
                        u8 parent = plan->states[base + comdat->associated];
                        if (parent != LINK_COMDAT_STATE_UNKNOWN)
                        {
                            plan->states[state_index] = parent;
                            progress = true;
                        }
                    }
                }
            }
        }
        for (u64 index = 0; error == LINK_ERROR_NONE && index < plan->count; index += 1)
        {
            if (plan->states[index] == LINK_COMDAT_STATE_UNKNOWN)
            {
                error = LINK_ERROR_INVALID_INPUT;
            }
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool link_comdat_is_discarded(LinkComdatPlan* plan, u32 object_index, u32 comdat)
{
    bool result = false;
    if (comdat)
    {
        result = plan->states[plan->object_offsets[object_index] + comdat - 1] == LINK_COMDAT_STATE_DISCARD;
    }
    return result;
}
'''


def patch_link_c() -> None:
    path = "src/buster/lib/compiler/link/link.c"
    text = read(path)
    text = replace_once(
        text,
        '''// The linker. Two public steps (link.h): link_objects merges ObjectFiles —
// section merging, symbol resolution (ObjectSymbol.weak decides which of two
// definitions survives instead of diagnosing a duplicate, and which
// references leave a symbol undefined-but-optional), relocation
// rebasing — into one
''',
        '''// The linker. Two public steps (link.h): link_objects merges ObjectFiles —
// section merging, deterministic COFF COMDAT group resolution, ordinary
// symbol resolution (ObjectSymbol.weak decides weak/strong precedence after
// a group winner is known), and relocation rebasing — into one
''',
        "link.c orientation",
    )
    marker = "BUSTER_GLOBAL_LOCAL bool link_symbol_definition_set(ObjectSymbol* destination, ObjectSymbol* source, ObjectFile* object, u64* section_offsets, Arena* arena)\n"
    if marker not in text:
        raise RuntimeError("link.c COMDAT insertion marker missing")
    text = text.replace(marker, COMDAT_LINK_CODE + "\n" + marker, 1)
    text = replace_once(
        text,
        '''    *destination = *source;
    destination->name = link_string_copy(arena, source->name);
''',
        '''    *destination = *source;
    destination->name = link_string_copy(arena, source->name);
    destination->comdat = 0;
''',
        "link.c clear merged symbol COMDAT",
    )
    start = text.index("LinkObjectResult link_objects(")
    end = text.index("ObjectFile link_windows_runtime_object", start)
    prefix, region, suffix = text[:start], text[start:end], text[end:]
    region = replace_once(
        region,
        '''        if (object->error != OBJECT_ERROR_NONE || !object->sections || object->section_count > OBJECT_SECTION_COUNT ||
            (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations) ||
            (object->debug_module_count && !object->debug_modules))
''',
        '''        if (object->error != OBJECT_ERROR_NONE || !object->sections || object->section_count > OBJECT_SECTION_COUNT ||
            (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations) ||
            (object->comdat_count && !object->comdats) || (object->debug_module_count && !object->debug_modules))
''',
        "link.c input COMDAT pointer validation",
    )
    allocation_marker = '''    result.object = (ObjectFile){
        .sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT),
'''
    plan_code = '''    LinkComdatPlan comdat_plan = {0};
    String8 comdat_error_symbol = {0};
    LinkError comdat_error = link_comdat_plan_build(arena, objects, object_count, &comdat_plan, &comdat_error_symbol);
    if (comdat_error != LINK_ERROR_NONE)
    {
        result.error = comdat_error;
        result.symbol = link_string_copy(arena, comdat_error_symbol);
        return result;
    }
'''
    region = replace_once(region, allocation_marker, plan_code + allocation_marker, "link.c COMDAT pre-resolution")
    old_validation = '''            if (source->section != OBJECT_SECTION_UNDEFINED &&
                (source->section >= object->section_count ||
                 source->value > BUSTER_MAX(object->sections[source->section].data.length, object->sections[source->section].virtual_size) ||
                 source->size > BUSTER_MAX(object->sections[source->section].data.length, object->sections[source->section].virtual_size) - source->value))
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            u32 destination_index = UINT32_MAX;
'''
    new_validation = '''            if (source->comdat > object->comdat_count ||
                (source->section != OBJECT_SECTION_UNDEFINED &&
                 (source->section >= object->section_count ||
                  source->value > BUSTER_MAX(object->sections[source->section].data.length, object->sections[source->section].virtual_size) ||
                  source->size > BUSTER_MAX(object->sections[source->section].data.length, object->sections[source->section].virtual_size) - source->value)))
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            ObjectSymbol discarded_source = {0};
            if (link_comdat_is_discarded(&comdat_plan, object_index, source->comdat))
            {
                if (!source->global)
                {
                    continue;
                }
                discarded_source = *source;
                discarded_source.value = 0;
                discarded_source.size = 0;
                discarded_source.section = OBJECT_SECTION_UNDEFINED;
                discarded_source.comdat = 0;
                discarded_source.weak = true;
                source = &discarded_source;
            }
            u32 destination_index = UINT32_MAX;
'''
    region = replace_once(region, old_validation, new_validation, "link.c discarded symbol handling")
    old_relocation_validation = '''            ObjectRelocation source = object->relocations[relocation_index];
            if (source.section >= object->section_count || source.symbol >= object->symbol_count || symbol_maps[object_index][source.symbol] == UINT32_MAX)
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
'''
    new_relocation_validation = '''            ObjectRelocation source = object->relocations[relocation_index];
            if (source.comdat > object->comdat_count)
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            if (link_comdat_is_discarded(&comdat_plan, object_index, source.comdat))
            {
                continue;
            }
            if (source.section >= object->section_count || source.symbol >= object->symbol_count || symbol_maps[object_index][source.symbol] == UINT32_MAX)
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
'''
    region = replace_once(region, old_relocation_validation, new_relocation_validation, "link.c discarded relocation handling")
    region = replace_once(
        region,
        '''            source.section = (u32)kind;
            source.offset += offsets[object->relocations[relocation_index].section];
            source.symbol = symbol_maps[object_index][source.symbol];
''',
        '''            source.section = (u32)kind;
            source.offset += offsets[object->relocations[relocation_index].section];
            source.symbol = symbol_maps[object_index][source.symbol];
            source.comdat = 0;
''',
        "link.c clear merged relocation COMDAT",
    )
    text = prefix + region + suffix
    text = replace_once(
        text,
        '''        S8_INITIALIZER("target mismatch"),
        S8_INITIALIZER("duplicate symbol"),
        S8_INITIALIZER("unresolved symbol"),
''',
        '''        S8_INITIALIZER("target mismatch"),
        S8_INITIALIZER("duplicate symbol"),
        S8_INITIALIZER("COMDAT selection mismatch"),
        S8_INITIALIZER("COMDAT size mismatch"),
        S8_INITIALIZER("COMDAT exact-match mismatch"),
        S8_INITIALIZER("unresolved symbol"),
''',
        "link.c error names",
    )
    write(path, text)


def patch_driver_diagnostic() -> None:
    path = "src/buster/lib/compiler/driver/driver_diagnostic.c"
    text = read(path)
    text = replace_once(
        text,
        '''        [LINK_ERROR_TARGET_MISMATCH] = S8_INITIALIZER("link.target-mismatch"),
        [LINK_ERROR_DUPLICATE_SYMBOL] = S8_INITIALIZER("link.duplicate-symbol"),
        [LINK_ERROR_UNRESOLVED_SYMBOL] = S8_INITIALIZER("link.unresolved-symbol"),
''',
        '''        [LINK_ERROR_TARGET_MISMATCH] = S8_INITIALIZER("link.target-mismatch"),
        [LINK_ERROR_DUPLICATE_SYMBOL] = S8_INITIALIZER("link.duplicate-symbol"),
        [LINK_ERROR_COMDAT_SELECTION_MISMATCH] = S8_INITIALIZER("link.comdat-selection-mismatch"),
        [LINK_ERROR_COMDAT_SIZE_MISMATCH] = S8_INITIALIZER("link.comdat-size-mismatch"),
        [LINK_ERROR_COMDAT_EXACT_MATCH] = S8_INITIALIZER("link.comdat-exact-match"),
        [LINK_ERROR_UNRESOLVED_SYMBOL] = S8_INITIALIZER("link.unresolved-symbol"),
''',
        "driver diagnostic COMDAT names",
    )
    write(path, text)


COMDAT_TEST_CODE = r'''
BUSTER_GLOBAL_LOCAL ObjectFile link_test_comdat_object(Arena* arena, Target target, ByteSlice text,
                                                       ObjectSymbol* symbols, u32 symbol_count,
                                                       ObjectRelocation* relocations, u32 relocation_count,
                                                       ObjectComdat* comdats, u32 comdat_count)
{
    ObjectFile result = link_test_object_make(arena, target, text, symbols, symbol_count, relocations, relocation_count);
    result.comdats = comdats;
    result.comdat_count = comdat_count;
    return result;
}

BUSTER_GLOBAL_LOCAL u32 link_test_comdat_symbol_find(ObjectFile* object, String8 name)
{
    u32 result = UINT32_MAX;
    for (u32 index = 0; object && result == UINT32_MAX && index < object->symbol_count; index += 1)
    {
        if (string_equal(object->symbols[index].name, name))
        {
            result = index;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ObjectFile link_test_single_comdat(Arena* arena, Target target, ByteSlice bytes, String8 key,
                                                       ObjectComdatSelection selection, ObjectSymbol* symbols,
                                                       u32 symbol_count, ObjectRelocation* relocations, u32 relocation_count)
{
    ObjectComdat* comdat = arena_allocate(arena, ObjectComdat, 1);
    *comdat = (ObjectComdat){
        .key = key,
        .size = bytes.length,
        .section = OBJECT_SECTION_TEXT,
        .associated = OBJECT_COMDAT_ASSOCIATED_NONE,
        .first_relocation = 0,
        .relocation_count = relocation_count,
        .selection = selection,
    };
    symbols[0].comdat = 1;
    symbols[0].weak = selection != OBJECT_COMDAT_SELECTION_NO_DUPLICATES;
    for (u32 index = 0; index < relocation_count; index += 1)
    {
        relocations[index].comdat = 1;
    }
    return link_test_comdat_object(arena, target, bytes, symbols, symbol_count, relocations, relocation_count, comdat, 1);
}

BUSTER_GLOBAL_LOCAL UnitTestResult link_test_coff_comdat_selection(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arena_create((ArenaCreation){0});
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS};
    {
        u8 left_bytes[] = {0x11, 0x12};
        u8 right_bytes[] = {0x21, 0x22};
        ObjectSymbol left_symbols[] = {{.name = S8("same"), .size = 2, .section = OBJECT_SECTION_TEXT,
                                        .kind = OBJECT_SYMBOL_FUNCTION, .global = true}};
        ObjectSymbol right_symbols[] = {{.name = S8("same"), .size = 2, .section = OBJECT_SECTION_TEXT,
                                         .kind = OBJECT_SYMBOL_FUNCTION, .global = true}};
        ObjectFile objects[] = {
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(left_bytes), S8("same"), OBJECT_COMDAT_SELECTION_SAME_SIZE,
                                    left_symbols, BUSTER_ARRAY_LENGTH(left_symbols), 0, 0),
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(right_bytes), S8("same"), OBJECT_COMDAT_SELECTION_SAME_SIZE,
                                    right_symbols, BUSTER_ARRAY_LENGTH(right_symbols), 0, 0),
        };
        LinkObjectResult linked = link_objects(arena, objects, BUSTER_ARRAY_LENGTH(objects), (LinkOptions){0});
        BUSTER_TEST(arguments, linked.error == LINK_ERROR_NONE);
        u32 symbol = link_test_comdat_symbol_find(&linked.object, S8("same"));
        BUSTER_TEST(arguments, symbol != UINT32_MAX && linked.object.symbols[symbol].size == 2 &&
                               linked.object.sections[OBJECT_SECTION_TEXT].data.pointer[linked.object.symbols[symbol].value] == 0x11);
        arena_reset_to_start(arena);
    }
    {
        u8 left_bytes[] = {1};
        u8 right_bytes[] = {2, 3};
        ObjectSymbol left_symbols[] = {{.name = S8("same_size_bad"), .size = 1, .section = OBJECT_SECTION_TEXT,
                                        .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectSymbol right_symbols[] = {{.name = S8("same_size_bad"), .size = 2, .section = OBJECT_SECTION_TEXT,
                                         .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectFile objects[] = {
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(left_bytes), S8("same_size_bad"), OBJECT_COMDAT_SELECTION_SAME_SIZE,
                                    left_symbols, 1, 0, 0),
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(right_bytes), S8("same_size_bad"), OBJECT_COMDAT_SELECTION_SAME_SIZE,
                                    right_symbols, 1, 0, 0),
        };
        LinkObjectResult linked = link_objects(arena, objects, 2, (LinkOptions){0});
        BUSTER_TEST(arguments, linked.error == LINK_ERROR_COMDAT_SIZE_MISMATCH && string_equal(linked.symbol, S8("same_size_bad")));
        arena_reset_to_start(arena);
    }
    {
        u8 left_bytes[] = {4, 5};
        u8 right_bytes[] = {4, 6};
        ObjectSymbol left_symbols[] = {{.name = S8("exact_bad"), .size = 2, .section = OBJECT_SECTION_TEXT,
                                        .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectSymbol right_symbols[] = {{.name = S8("exact_bad"), .size = 2, .section = OBJECT_SECTION_TEXT,
                                         .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectFile objects[] = {
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(left_bytes), S8("exact_bad"), OBJECT_COMDAT_SELECTION_EXACT_MATCH,
                                    left_symbols, 1, 0, 0),
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(right_bytes), S8("exact_bad"), OBJECT_COMDAT_SELECTION_EXACT_MATCH,
                                    right_symbols, 1, 0, 0),
        };
        LinkObjectResult linked = link_objects(arena, objects, 2, (LinkOptions){0});
        BUSTER_TEST(arguments, linked.error == LINK_ERROR_COMDAT_EXACT_MATCH && string_equal(linked.symbol, S8("exact_bad")));
        arena_reset_to_start(arena);
    }
    {
        u8 bytes[] = {0, 0, 0, 0};
        ObjectSymbol left_symbols[] = {
            {.name = S8("exact_reloc"), .size = 4, .section = OBJECT_SECTION_TEXT, .kind = OBJECT_SYMBOL_DATA, .global = true},
            {.name = S8("target_a"), .section = OBJECT_SECTION_UNDEFINED, .kind = OBJECT_SYMBOL_DATA, .global = true},
        };
        ObjectSymbol right_symbols[] = {
            {.name = S8("exact_reloc"), .size = 4, .section = OBJECT_SECTION_TEXT, .kind = OBJECT_SYMBOL_DATA, .global = true},
            {.name = S8("target_b"), .section = OBJECT_SECTION_UNDEFINED, .kind = OBJECT_SYMBOL_DATA, .global = true},
        };
        ObjectRelocation left_relocations[] = {{.section = OBJECT_SECTION_TEXT, .symbol = 1, .kind = OBJECT_RELOCATION_ABSOLUTE32}};
        ObjectRelocation right_relocations[] = {{.section = OBJECT_SECTION_TEXT, .symbol = 1, .kind = OBJECT_RELOCATION_ABSOLUTE32}};
        ObjectFile objects[] = {
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(bytes), S8("exact_reloc"), OBJECT_COMDAT_SELECTION_EXACT_MATCH,
                                    left_symbols, 2, left_relocations, 1),
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(bytes), S8("exact_reloc"), OBJECT_COMDAT_SELECTION_EXACT_MATCH,
                                    right_symbols, 2, right_relocations, 1),
        };
        LinkObjectResult linked = link_objects(arena, objects, 2, (LinkOptions){.allow_undefined_symbols = true});
        BUSTER_TEST(arguments, linked.error == LINK_ERROR_COMDAT_EXACT_MATCH);
        arena_reset_to_start(arena);
    }
    for (u32 order = 0; order < 2; order += 1)
    {
        u8 small_bytes[] = {0x31};
        u8 large_bytes[] = {0x42, 0x43, 0x44};
        ObjectSymbol small_symbols[] = {{.name = S8("largest"), .size = 1, .section = OBJECT_SECTION_TEXT,
                                         .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectSymbol large_symbols[] = {{.name = S8("largest"), .size = 3, .section = OBJECT_SECTION_TEXT,
                                         .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectFile small = link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(small_bytes), S8("largest"), OBJECT_COMDAT_SELECTION_LARGEST,
                                                    small_symbols, 1, 0, 0);
        ObjectFile large = link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(large_bytes), S8("largest"), OBJECT_COMDAT_SELECTION_LARGEST,
                                                    large_symbols, 1, 0, 0);
        ObjectFile objects[2] = {order ? large : small, order ? small : large};
        LinkObjectResult linked = link_objects(arena, objects, 2, (LinkOptions){0});
        u32 symbol = link_test_comdat_symbol_find(&linked.object, S8("largest"));
        BUSTER_TEST(arguments, linked.error == LINK_ERROR_NONE && symbol != UINT32_MAX && linked.object.symbols[symbol].size == 3);
        if (linked.error == LINK_ERROR_NONE && symbol != UINT32_MAX)
        {
            BUSTER_TEST(arguments, linked.object.sections[OBJECT_SECTION_TEXT].data.pointer[linked.object.symbols[symbol].value] == 0x42);
        }
        arena_reset_to_start(arena);
    }
    {
        u8 left_bytes[] = {0x51, 0xa1};
        u8 right_bytes[] = {0x62, 0x63, 0xb2};
        ObjectSymbol left_symbols[] = {
            {.name = S8("assoc_parent"), .size = 1, .section = OBJECT_SECTION_TEXT, .comdat = 1,
             .kind = OBJECT_SYMBOL_DATA, .global = true, .weak = true},
            {.name = S8("assoc_child"), .value = 1, .size = 1, .section = OBJECT_SECTION_TEXT, .comdat = 2,
             .kind = OBJECT_SYMBOL_DATA, .global = true, .weak = true},
            {.name = S8("loser_target"), .section = OBJECT_SECTION_UNDEFINED, .kind = OBJECT_SYMBOL_DATA, .global = true},
        };
        ObjectSymbol right_symbols[] = {
            {.name = S8("assoc_parent"), .size = 2, .section = OBJECT_SECTION_TEXT, .comdat = 1,
             .kind = OBJECT_SYMBOL_DATA, .global = true, .weak = true},
            {.name = S8("assoc_child"), .value = 2, .size = 1, .section = OBJECT_SECTION_TEXT, .comdat = 2,
             .kind = OBJECT_SYMBOL_DATA, .global = true, .weak = true},
            {.name = S8("winner_target"), .section = OBJECT_SECTION_UNDEFINED, .kind = OBJECT_SYMBOL_DATA, .global = true},
        };
        ObjectRelocation left_relocations[] = {{.offset = 1, .section = OBJECT_SECTION_TEXT, .symbol = 2, .comdat = 2,
                                                 .kind = OBJECT_RELOCATION_ABSOLUTE32}};
        ObjectRelocation right_relocations[] = {{.offset = 2, .section = OBJECT_SECTION_TEXT, .symbol = 2, .comdat = 2,
                                                  .kind = OBJECT_RELOCATION_ABSOLUTE32}};
        ObjectComdat left_comdats[] = {
            {.key = S8("assoc_parent"), .size = 1, .section = OBJECT_SECTION_TEXT, .associated = OBJECT_COMDAT_ASSOCIATED_NONE,
             .selection = OBJECT_COMDAT_SELECTION_LARGEST},
            {.offset = 1, .size = 1, .section = OBJECT_SECTION_TEXT, .associated = 0, .first_relocation = 0, .relocation_count = 1,
             .selection = OBJECT_COMDAT_SELECTION_ASSOCIATIVE},
        };
        ObjectComdat right_comdats[] = {
            {.key = S8("assoc_parent"), .size = 2, .section = OBJECT_SECTION_TEXT, .associated = OBJECT_COMDAT_ASSOCIATED_NONE,
             .selection = OBJECT_COMDAT_SELECTION_LARGEST},
            {.offset = 2, .size = 1, .section = OBJECT_SECTION_TEXT, .associated = 0, .first_relocation = 0, .relocation_count = 1,
             .selection = OBJECT_COMDAT_SELECTION_ASSOCIATIVE},
        };
        ObjectFile objects[] = {
            link_test_comdat_object(arena, target, BUSTER_ARRAY_TO_SLICE(left_bytes), left_symbols, 3, left_relocations, 1, left_comdats, 2),
            link_test_comdat_object(arena, target, BUSTER_ARRAY_TO_SLICE(right_bytes), right_symbols, 3, right_relocations, 1, right_comdats, 2),
        };
        LinkObjectResult linked = link_objects(arena, objects, 2, (LinkOptions){.allow_undefined_symbols = true});
        u32 child = link_test_comdat_symbol_find(&linked.object, S8("assoc_child"));
        BUSTER_TEST(arguments, linked.error == LINK_ERROR_NONE && child != UINT32_MAX && linked.object.relocation_count == 1);
        if (linked.error == LINK_ERROR_NONE && child != UINT32_MAX)
        {
            BUSTER_TEST(arguments, linked.object.sections[OBJECT_SECTION_TEXT].data.pointer[linked.object.symbols[child].value] == 0xb2);
            ObjectRelocation relocation = linked.object.relocations[0];
            BUSTER_TEST(arguments, string_equal(linked.object.symbols[relocation.symbol].name, S8("winner_target")));
        }
        arena_reset_to_start(arena);
    }
    {
        u8 bytes[] = {1};
        ObjectSymbol left_symbols[] = {{.name = S8("mode_bad"), .size = 1, .section = OBJECT_SECTION_TEXT,
                                        .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectSymbol right_symbols[] = {{.name = S8("mode_bad"), .size = 1, .section = OBJECT_SECTION_TEXT,
                                         .kind = OBJECT_SYMBOL_DATA, .global = true}};
        ObjectFile objects[] = {
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(bytes), S8("mode_bad"), OBJECT_COMDAT_SELECTION_ANY,
                                    left_symbols, 1, 0, 0),
            link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(bytes), S8("mode_bad"), OBJECT_COMDAT_SELECTION_LARGEST,
                                    right_symbols, 1, 0, 0),
        };
        LinkObjectResult linked = link_objects(arena, objects, 2, (LinkOptions){0});
        BUSTER_TEST(arguments, linked.error == LINK_ERROR_COMDAT_SELECTION_MISMATCH);
        arena_reset_to_start(arena);
    }
    arena_destroy(arena, 1);
    return result;
}
'''


def patch_link_test() -> None:
    path = "src/buster/tests/compiler/link/link_test.c"
    text = read(path)
    marker = "BUSTER_GLOBAL_LOCAL UnitTestResult link_test_initializer_order(UnitTestArguments* arguments)\n"
    if marker not in text:
        raise RuntimeError("link_test COMDAT insertion marker missing")
    text = text.replace(marker, COMDAT_TEST_CODE + "\n" + marker, 1)
    text = replace_once(
        text,
        '''    UnitTestResult initializer_order = link_test_initializer_order(arguments);
    result.succeeded_test_count += initializer_order.succeeded_test_count;
    result.test_count += initializer_order.test_count;
''',
        '''    UnitTestResult comdat_selection = link_test_coff_comdat_selection(arguments);
    result.succeeded_test_count += comdat_selection.succeeded_test_count;
    result.test_count += comdat_selection.test_count;
    UnitTestResult initializer_order = link_test_initializer_order(arguments);
    result.succeeded_test_count += initializer_order.succeeded_test_count;
    result.test_count += initializer_order.test_count;
''',
        "link_test register COMDAT test",
    )
    write(path, text)


def patch_object_test() -> None:
    path = "src/buster/tests/compiler/object/object_test.c"
    text = read(path)
    text = replace_once(
        text,
        '''            BUSTER_STRING_TEST(arguments, comdat.symbols[5].name, S8("pending"));
            BUSTER_TEST(arguments, comdat.symbols[5].global && !comdat.symbols[5].weak);
''',
        '''            BUSTER_STRING_TEST(arguments, comdat.symbols[5].name, S8("pending"));
            BUSTER_TEST(arguments, comdat.symbols[5].global && !comdat.symbols[5].weak);
            BUSTER_TEST(arguments, comdat.comdat_count == 3);
            if (comdat.comdat_count == 3)
            {
                BUSTER_STRING_TEST(arguments, comdat.comdats[0].key, S8("any_one"));
                BUSTER_TEST(arguments, comdat.comdats[0].selection == OBJECT_COMDAT_SELECTION_ANY &&
                                       comdat.comdats[0].section == OBJECT_SECTION_READ_ONLY_DATA &&
                                       comdat.symbols[2].comdat == 1);
                BUSTER_STRING_TEST(arguments, comdat.comdats[1].key, S8("strict1"));
                BUSTER_TEST(arguments, comdat.comdats[1].selection == OBJECT_COMDAT_SELECTION_NO_DUPLICATES &&
                                       comdat.symbols[4].comdat == 2);
                BUSTER_TEST(arguments, comdat.comdats[2].selection == OBJECT_COMDAT_SELECTION_NONE &&
                                       comdat.symbols[5].comdat == 3);
            }
''',
        "object_test COMDAT metadata assertions",
    )
    write(path, text)


def patch_docs() -> None:
    path = "docs/agents/frontend/linkage.md"
    text = read(path)
    old = '''  `STB_WEAK` and Mach-O as `N_WEAK_DEF`. COFF spells a weak definition as a
  selectany COMDAT, which needs a section per symbol while this model merges
  sections by kind, so a COFF object reads `weak` back but cannot write it and
  carries such a symbol as an ordinary external. That is the one gap of the
'''
    new = '''  `STB_WEAK` and Mach-O as `N_WEAK_DEF`. COFF spells a weak definition as a
  selectany COMDAT, which needs a section per symbol while this model merges
  sections by kind, so the writer still cannot synthesize it. The reader does
  preserve each source contribution's key, selection, associated parent, byte
  range and relocation range. `link_objects` resolves those groups first:
  ANY keeps one, SAME_SIZE and EXACT_MATCH validate their contracts, LARGEST
  chooses by size independently of input order, and ASSOCIATIVE follows its
  parent. Only then does the surviving definition enter ordinary weak/strong
  arbitration. A COFF object therefore reads `weak` back but cannot write it
  and carries a compiler-produced weak symbol as an ordinary external. That is the one gap of the
'''
    text = replace_once(text, old, new, "linkage COMDAT documentation")
    write(path, text)


def main() -> None:
    patch_object_h()
    patch_object_c()
    patch_link_h()
    patch_link_c()
    patch_driver_diagnostic()
    patch_link_test()
    patch_object_test()
    patch_docs()


if __name__ == "__main__":
    main()
