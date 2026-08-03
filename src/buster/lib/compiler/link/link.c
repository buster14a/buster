#include <buster/lib/compiler/link/link.h>

#include <buster/lib/compiler/pdb/pdb.h>

#include <buster/lib/file.h>
#include <buster/lib/integer.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL ObjectSectionKind const link_elf_debug_kinds[] = {
    OBJECT_SECTION_DEBUG_INFO,
    OBJECT_SECTION_DEBUG_ABBREV,
    OBJECT_SECTION_DEBUG_LINE,
    OBJECT_SECTION_DEBUG_STR,
    OBJECT_SECTION_DEBUG_LOC,
    OBJECT_SECTION_DEBUG_RANGES,
};

BUSTER_GLOBAL_LOCAL ObjectSectionKind const link_elf_loaded_kinds[] = {
    OBJECT_SECTION_TEXT,
    OBJECT_SECTION_READ_ONLY_DATA,
    OBJECT_SECTION_DATA,
    OBJECT_SECTION_ZERO,
    OBJECT_SECTION_THREAD_LOCAL_DATA,
    OBJECT_SECTION_THREAD_LOCAL_ZERO,
    OBJECT_SECTION_UNWIND,
};

#define BUSTER_LINK_ELF_SECTION_HEADER_SIZE 64
#define BUSTER_LINK_ELF_DYNAMIC_SECTION_COUNT 8
#define BUSTER_LINK_ELF_SECTION_DESCRIPTOR_CAPACITY                                                                                                          \
    (BUSTER_ARRAY_LENGTH(link_elf_loaded_kinds) + 1 + BUSTER_LINK_ELF_DYNAMIC_SECTION_COUNT + BUSTER_ARRAY_LENGTH(link_elf_debug_kinds))

typedef struct LinkElfSectionTableLayout LinkElfSectionTableLayout;
struct LinkElfSectionTableLayout
{
    u64 eh_frame_header_offset;
    u64 eh_frame_header_size;
    u64 interpreter_offset;
    u64 interpreter_size;
    u64 plt_offset;
    u64 plt_size;
    u64 dynamic_string_offset;
    u64 dynamic_string_size;
    u64 dynamic_symbol_offset;
    u64 dynamic_symbol_size;
    u64 hash_offset;
    u64 hash_size;
    u64 relocation_offset;
    u64 relocation_size;
    u64 got_offset;
    u64 got_size;
    u64 dynamic_offset;
    u64 dynamic_size;
    bool dynamic;
    u8 reserved[7];
};

typedef struct LinkElfEhFrameEntry LinkElfEhFrameEntry;
struct LinkElfEhFrameEntry
{
    u64 fde_offset;
    u64 function_address;
    u32 relocation;
    u32 reserved;
};

typedef struct LinkElfEhFrameTable LinkElfEhFrameTable;
struct LinkElfEhFrameTable
{
    LinkElfEhFrameEntry* entries;
    LinkElfEhFrameEntry* scratch;
    u32 count;
    bool valid;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL String8 link_string_copy(Arena* arena, String8 source)
{
    String8 result = {0};
    if (source.length)
    {
        result.pointer = arena_allocate(arena, char8, source.length);
        result.length = source.length;
        memcpy(result.pointer, source.pointer, source.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool link_target_matches(Target left, Target right)
{
    return left.cpu_arch == right.cpu_arch && left.os == right.os;
}

BUSTER_GLOBAL_LOCAL u32 link_global_symbol_find(ObjectSymbol* symbols, u32 symbol_count, String8 name)
{
    for (u32 index = 0; index < symbol_count; index += 1)
    {
        if (symbols[index].global && string_equal(symbols[index].name, name))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool link_symbol_definition_set(ObjectSymbol* destination, ObjectSymbol* source, ObjectFile* object, u64* section_offsets, Arena* arena)
{
    *destination = *source;
    destination->name = link_string_copy(arena, source->name);
    if (source->section == OBJECT_SECTION_UNDEFINED)
    {
        return true;
    }
    if (source->section >= object->section_count)
    {
        return false;
    }
    ObjectSectionKind kind = object->sections[source->section].kind;
    if (kind >= OBJECT_SECTION_COUNT)
    {
        return false;
    }
    destination->section = (u32)kind;
    destination->value += section_offsets[source->section];
    return true;
}

LinkObjectResult link_objects(Arena* arena, ObjectFile* objects, u32 object_count, LinkOptions options)
{
    LinkObjectResult result = {0};
    if (!arena || !objects || !object_count)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    Target target = objects[0].target;
    u64* section_sizes = arena_allocate(arena, u64, OBJECT_SECTION_COUNT);
    u32* section_alignments = arena_allocate(arena, u32, OBJECT_SECTION_COUNT);
    memset(section_sizes, 0, sizeof(*section_sizes) * OBJECT_SECTION_COUNT);
    memset(section_alignments, 0, sizeof(*section_alignments) * OBJECT_SECTION_COUNT);
    section_alignments[OBJECT_SECTION_TEXT] = 1;
    section_alignments[OBJECT_SECTION_READ_ONLY_DATA] = 1;
    section_alignments[OBJECT_SECTION_DATA] = 1;
    section_alignments[OBJECT_SECTION_ZERO] = 1;
    section_alignments[OBJECT_SECTION_THREAD_LOCAL_DATA] = 1;
    section_alignments[OBJECT_SECTION_THREAD_LOCAL_ZERO] = 1;
    section_alignments[OBJECT_SECTION_UNWIND] = 1;
    u64 total_symbols = 0;
    u64 total_relocations = 0;
    u64 total_debug_modules = 0;
    u64 offset_count = (u64)object_count * OBJECT_SECTION_COUNT;
    u64* section_offsets = arena_allocate(arena, u64, offset_count);
    for (u32 object_index = 0; object_index < object_count; object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        if (object->error != OBJECT_ERROR_NONE || !object->sections || object->section_count > OBJECT_SECTION_COUNT ||
            (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations) ||
            (object->debug_module_count && !object->debug_modules))
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        if (!link_target_matches(target, object->target))
        {
            result.error = LINK_ERROR_TARGET_MISMATCH;
            return result;
        }
        total_symbols += object->symbol_count;
        total_relocations += object->relocation_count;
        total_debug_modules += object->debug_module_count;
        if (total_symbols > UINT32_MAX || total_relocations > UINT32_MAX || total_debug_modules > UINT32_MAX)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        for (u32 section_index = 0; section_index < object->section_count; section_index += 1)
        {
            ObjectSection* section = &object->sections[section_index];
            if (section->kind >= OBJECT_SECTION_COUNT || !section->alignment || !BUSTER_IS_POWER_OF_TWO(section->alignment) ||
                (section->data.length && !section->data.pointer))
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            u32 alignment = section->alignment;
            u64 section_size = BUSTER_MAX(section->data.length, section->virtual_size);
            u64 aligned = align_forward(section_sizes[section->kind], alignment);
            if (aligned < section_sizes[section->kind] || section_size > UINT64_MAX - aligned)
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            section_offsets[(u64)object_index * OBJECT_SECTION_COUNT + section_index] = aligned;
            section_sizes[section->kind] = aligned + section_size;
            section_alignments[section->kind] = BUSTER_MAX(section_alignments[section->kind], alignment);
        }
    }
    result.object = (ObjectFile){
        .sections = arena_allocate(arena, ObjectSection, OBJECT_SECTION_COUNT),
        .symbols = arena_allocate(arena, ObjectSymbol, total_symbols),
        .relocations = arena_allocate(arena, ObjectRelocation, total_relocations),
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
        .debug_modules = arena_allocate(arena, ObjectDebugModule, total_debug_modules),
    };
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        bool zero_fill = object_section_kind_is_zero_fill((ObjectSectionKind)kind);
        u8* data = zero_fill ? 0 : arena_allocate(arena, u8, section_sizes[kind]);
        result.object.sections[kind] = (ObjectSection){
            .name = object_section_name_for_kind((ObjectSectionKind)kind),
            .data =
                {
                    .pointer = data,
                    .length = zero_fill ? 0 : section_sizes[kind],
                },
            .virtual_size = section_sizes[kind],
            .kind = (ObjectSectionKind)kind,
            .alignment = section_alignments[kind],
        };
    }
    for (u32 object_index = 0; object_index < object_count; object_index += 1)
    {
        ObjectFile* object = objects + object_index;
        u64* offsets = section_offsets + (u64)object_index * OBJECT_SECTION_COUNT;
        for (u32 module_index = 0; module_index < object->debug_module_count; module_index += 1)
        {
            ObjectDebugModule source = object->debug_modules[module_index];
            ObjectDebugModule* destination = result.object.debug_modules + result.object.debug_module_count++;
            *destination = source;
            destination->name = link_string_copy(arena, source.name);
            destination->code_offset += offsets[OBJECT_SECTION_TEXT];
            destination->symbols_offset += offsets[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS];
            destination->types_offset += offsets[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES];
        }
    }
    for (u32 object_index = 0; object_index < object_count; object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u64* offsets = section_offsets + (u64)object_index * OBJECT_SECTION_COUNT;
        for (u32 section_index = 0; section_index < object->section_count; section_index += 1)
        {
            ObjectSection* source = &object->sections[section_index];
            if (source->data.length)
            {
                memcpy(result.object.sections[source->kind].data.pointer + offsets[section_index], source->data.pointer, source->data.length);
            }
        }
    }
    u32** symbol_maps = arena_allocate(arena, u32*, object_count);
    for (u32 object_index = 0; object_index < object_count; object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u32* symbol_map = arena_allocate(arena, u32, object->symbol_count);
        symbol_maps[object_index] = symbol_map;
        for (u32 source_index = 0; source_index < object->symbol_count; source_index += 1)
        {
            symbol_map[source_index] = UINT32_MAX;
            ObjectSymbol* source = &object->symbols[source_index];
            if (!source->name.length)
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            if (source->section != OBJECT_SECTION_UNDEFINED &&
                (source->section >= object->section_count ||
                 source->value > BUSTER_MAX(object->sections[source->section].data.length, object->sections[source->section].virtual_size) ||
                 source->size > BUSTER_MAX(object->sections[source->section].data.length, object->sections[source->section].virtual_size) - source->value))
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            u32 destination_index = UINT32_MAX;
            if (source->global)
            {
                destination_index = link_global_symbol_find(result.object.symbols, result.object.symbol_count, source->name);
            }
            if (destination_index == UINT32_MAX)
            {
                destination_index = result.object.symbol_count++;
                if (!link_symbol_definition_set(&result.object.symbols[destination_index], source, object,
                                                section_offsets + (u64)object_index * OBJECT_SECTION_COUNT, arena))
                {
                    result.error = LINK_ERROR_INVALID_INPUT;
                    return result;
                }
            }
            else
            {
                ObjectSymbol* destination = &result.object.symbols[destination_index];
                bool destination_defined = destination->section != OBJECT_SECTION_UNDEFINED;
                bool source_defined = source->section != OBJECT_SECTION_UNDEFINED;
                if (destination_defined && source_defined)
                {
                    result.error = LINK_ERROR_DUPLICATE_SYMBOL;
                    result.symbol = link_string_copy(arena, source->name);
                    return result;
                }
                if (!destination_defined && source_defined)
                {
                    if (!link_symbol_definition_set(destination, source, object, section_offsets + (u64)object_index * OBJECT_SECTION_COUNT, arena))
                    {
                        result.error = LINK_ERROR_INVALID_INPUT;
                        return result;
                    }
                }
            }
            symbol_map[source_index] = destination_index;
        }
    }
    for (u32 object_index = 0; object_index < object_count; object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u64* offsets = section_offsets + (u64)object_index * OBJECT_SECTION_COUNT;
        for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
        {
            ObjectRelocation source = object->relocations[relocation_index];
            if (source.section >= object->section_count || source.symbol >= object->symbol_count || symbol_maps[object_index][source.symbol] == UINT32_MAX)
            {
                result.error = LINK_ERROR_INVALID_INPUT;
                return result;
            }
            ObjectSectionKind kind = object->sections[source.section].kind;
            source.section = (u32)kind;
            source.offset += offsets[object->relocations[relocation_index].section];
            source.symbol = symbol_maps[object_index][source.symbol];
            result.object.relocations[result.object.relocation_count++] = source;
        }
    }
    if (!options.allow_undefined_symbols)
    {
        for (u32 symbol_index = 0; symbol_index < result.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = &result.object.symbols[symbol_index];
            if (symbol->section == OBJECT_SECTION_UNDEFINED)
            {
                result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
                result.symbol = link_string_copy(arena, symbol->name);
                return result;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void link_write_u16(u8* bytes, u64 offset, u16 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void link_write_u32(u8* bytes, u64 offset, u32 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void link_write_u64(u8* bytes, u64 offset, u64 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_TEST_F_DECL u64 link_read_u64(u8 const* bytes, u64 offset)
{
    u64 result = 0;
    memcpy(&result, bytes + offset, sizeof(result));
    return result;
}

BUSTER_TEST_F_DECL u32 link_read_u32(u8 const* bytes, u64 offset)
{
    u32 result = 0;
    memcpy(&result, bytes + offset, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL void link_write_u32_be(u8* bytes, u64 offset, u32 value)
{
    bytes[offset] = (u8)(value >> 24);
    bytes[offset + 1] = (u8)(value >> 16);
    bytes[offset + 2] = (u8)(value >> 8);
    bytes[offset + 3] = (u8)value;
}

BUSTER_GLOBAL_LOCAL u32 link_rotate_right_u32(u32 value, u32 amount)
{
    return (value >> amount) | (value << (32 - amount));
}

BUSTER_TEST_F_DECL void link_sha256(Arena* arena, u8 const* input, u64 length, u8* output)
{
    static u32 const constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    u32* state = arena_allocate(arena, u32, 8);
    state[0] = 0x6a09e667;
    state[1] = 0xbb67ae85;
    state[2] = 0x3c6ef372;
    state[3] = 0xa54ff53a;
    state[4] = 0x510e527f;
    state[5] = 0x9b05688c;
    state[6] = 0x1f83d9ab;
    state[7] = 0x5be0cd19;
    u8* block = arena_allocate(arena, u8, 64);
    u32* words = arena_allocate(arena, u32, 64);
    u64 block_count = (length + 9 + 63) / 64;
    for (u64 block_index = 0; block_index < block_count; block_index += 1)
    {
        memset(block, 0, 64);
        u64 block_offset = block_index * 64;
        for (u64 byte_index = 0; byte_index < 64; byte_index += 1)
        {
            u64 source_offset = block_offset + byte_index;
            if (source_offset < length)
            {
                block[byte_index] = input[source_offset];
            }
            else if (source_offset == length)
            {
                block[byte_index] = 0x80;
            }
        }
        if (block_index + 1 == block_count)
        {
            u64 bit_length = length * 8;
            for (u32 index = 0; index < 8; index += 1)
            {
                block[63 - index] = (u8)(bit_length >> (index * 8));
            }
        }
        memset(words, 0, 64 * sizeof(*words));
        for (u32 index = 0; index < 16; index += 1)
        {
            u32 offset = index * 4;
            words[index] = ((u32)block[offset] << 24) | ((u32)block[offset + 1] << 16) | ((u32)block[offset + 2] << 8) | block[offset + 3];
        }
        for (u32 index = 16; index < 64; index += 1)
        {
            u32 first = link_rotate_right_u32(words[index - 15], 7) ^ link_rotate_right_u32(words[index - 15], 18) ^ (words[index - 15] >> 3);
            u32 second = link_rotate_right_u32(words[index - 2], 17) ^ link_rotate_right_u32(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + first + words[index - 7] + second;
        }
        u32 a = state[0];
        u32 b = state[1];
        u32 c = state[2];
        u32 d = state[3];
        u32 e = state[4];
        u32 f = state[5];
        u32 g = state[6];
        u32 h = state[7];
        for (u32 index = 0; index < 64; index += 1)
        {
            u32 sigma1 = link_rotate_right_u32(e, 6) ^ link_rotate_right_u32(e, 11) ^ link_rotate_right_u32(e, 25);
            u32 choice = (e & f) ^ (~e & g);
            u32 temporary1 = h + sigma1 + choice + constants[index] + words[index];
            u32 sigma0 = link_rotate_right_u32(a, 2) ^ link_rotate_right_u32(a, 13) ^ link_rotate_right_u32(a, 22);
            u32 majority = (a & b) ^ (a & c) ^ (b & c);
            u32 temporary2 = sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
    for (u32 index = 0; index < 8; index += 1)
    {
        link_write_u32_be(output, (u64)index * 4, state[index]);
    }
}

BUSTER_GLOBAL_LOCAL bool link_write_executable_file(String8 path, ByteSlice bytes)
{
    OsFileDescriptor* file = os_file_open(path,
                                          (OpenFlags){
                                              .write = 1,
                                              .create = 1,
                                              .truncate = 1,
                                          },
                                          (OpenPermissions){
                                              .read = 1,
                                              .write = 1,
                                              .execute = 1,
                                          });
    if (!file)
    {
        return false;
    }
    os_file_write(file, bytes);
    return os_file_close(file);
}

BUSTER_GLOBAL_LOCAL u32 link_symbol_find(ObjectFile* object, String8 name)
{
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        if (string_equal(object->symbols[index].name, name))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool link_elf_uleb_read(ByteSlice data, u64 end, u64* cursor, u64* value)
{
    u64 result = 0;
    u32 shift = 0;
    for (u32 byte_index = 0; byte_index < 10; byte_index += 1)
    {
        if (*cursor >= end || *cursor >= data.length)
        {
            return false;
        }
        u8 byte = data.pointer[(*cursor)++];
        u64 payload = byte & 0x7f;
        if (shift >= 64 || payload > (UINT64_MAX >> shift))
        {
            return false;
        }
        result |= payload << shift;
        if (!(byte & 0x80))
        {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool link_elf_cie_has_pcrel_sdata4(ByteSlice data, u64 cie_offset)
{
    if (cie_offset > data.length || data.length - cie_offset < 12)
    {
        return false;
    }
    u32 length = link_read_u32(data.pointer, cie_offset);
    if (length < 8 || length > data.length - cie_offset - 4 || link_read_u32(data.pointer, cie_offset + 4) != 0)
    {
        return false;
    }
    u64 end = cie_offset + 4 + length;
    u64 cursor = cie_offset + 8;
    if (data.pointer[cursor++] != 1 || end - cursor < 3 || data.pointer[cursor] != 'z' || data.pointer[cursor + 1] != 'R' || data.pointer[cursor + 2] != 0)
    {
        return false;
    }
    cursor += 3;
    u64 ignored = 0;
    if (!link_elf_uleb_read(data, end, &cursor, &ignored) || !link_elf_uleb_read(data, end, &cursor, &ignored) ||
        !link_elf_uleb_read(data, end, &cursor, &ignored))
    {
        return false;
    }
    u64 augmentation_size = 0;
    return link_elf_uleb_read(data, end, &cursor, &augmentation_size) && augmentation_size == 1 && cursor < end && data.pointer[cursor] == 0x1b;
}

BUSTER_GLOBAL_LOCAL LinkElfEhFrameTable link_elf_eh_frame_table_build(Arena* arena, ObjectFile* object)
{
    LinkElfEhFrameTable result = {0};
    if (!arena || !object || object->section_count <= OBJECT_SECTION_UNWIND)
    {
        return result;
    }
    ByteSlice data = object->sections[OBJECT_SECTION_UNWIND].data;
    if (!data.length)
    {
        result.valid = true;
        return result;
    }
    if (object->relocation_count > (UINT32_C(1) << 30))
    {
        return result;
    }
    result.entries = arena_allocate(arena, LinkElfEhFrameEntry, object->relocation_count);
    result.scratch = arena_allocate(arena, LinkElfEhFrameEntry, object->relocation_count);
    u32 relocation_map_capacity = 1;
    while (relocation_map_capacity < object->relocation_count * 2)
    {
        relocation_map_capacity <<= 1;
    }
    u32* relocation_map = arena_allocate(arena, u32, relocation_map_capacity);
    memset(relocation_map, 0xff, sizeof(*relocation_map) * relocation_map_capacity);
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation* relocation = object->relocations + relocation_index;
        if (relocation->section != OBJECT_SECTION_UNWIND ||
            (relocation->kind != OBJECT_RELOCATION_X86_64_PC32 && relocation->kind != OBJECT_RELOCATION_AARCH64_PREL32))
        {
            continue;
        }
        u32 slot = (u32)((relocation->offset ^ (relocation->offset >> 32)) * UINT64_C(11400714819323198485)) & (relocation_map_capacity - 1);
        while (relocation_map[slot] != UINT32_MAX)
        {
            if (object->relocations[relocation_map[slot]].offset == relocation->offset)
            {
                return result;
            }
            slot = (slot + 1) & (relocation_map_capacity - 1);
        }
        relocation_map[slot] = relocation_index;
    }
    u64 cursor = 0;
    while (cursor < data.length)
    {
        if (data.length - cursor < 4)
        {
            return result;
        }
        u32 length = link_read_u32(data.pointer, cursor);
        if (!length)
        {
            cursor += 4;
            continue;
        }
        if (length == UINT32_MAX || length > data.length - cursor - 4 || length < 4)
        {
            return result;
        }
        u64 record_end = cursor + 4 + length;
        u32 cie_pointer = link_read_u32(data.pointer, cursor + 4);
        if (cie_pointer)
        {
            u64 cie_pointer_offset = cursor + 4;
            if (cie_pointer > cie_pointer_offset || record_end - cursor < 12 || !link_elf_cie_has_pcrel_sdata4(data, cie_pointer_offset - cie_pointer))
            {
                return result;
            }
            u64 initial_location = cursor + 8;
            u32 relocation_slot = (u32)((initial_location ^ (initial_location >> 32)) * UINT64_C(11400714819323198485)) & (relocation_map_capacity - 1);
            while (relocation_map[relocation_slot] != UINT32_MAX &&
                   object->relocations[relocation_map[relocation_slot]].offset != initial_location)
            {
                relocation_slot = (relocation_slot + 1) & (relocation_map_capacity - 1);
            }
            u32 matched_relocation = relocation_map[relocation_slot];
            if (matched_relocation == UINT32_MAX || object->relocations[matched_relocation].symbol >= object->symbol_count || result.count == UINT32_MAX)
            {
                return result;
            }
            result.entries[result.count++] = (LinkElfEhFrameEntry){
                .fde_offset = cursor,
                .relocation = matched_relocation,
            };
        }
        cursor = record_end;
    }
    result.valid = true;
    return result;
}

BUSTER_GLOBAL_LOCAL bool link_elf_signed_difference(u64 left, u64 right, s32* output)
{
    if (left >= right)
    {
        u64 difference = left - right;
        if (difference > INT32_MAX)
        {
            return false;
        }
        *output = (s32)difference;
        return true;
    }
    u64 difference = right - left;
    if (difference > UINT64_C(2147483648))
    {
        return false;
    }
    *output = difference == UINT64_C(2147483648) ? INT32_MIN : -(s32)difference;
    return true;
}

BUSTER_GLOBAL_LOCAL bool link_elf_eh_frame_header_write(u8* bytes, u64 byte_count, LinkElfEhFrameTable* table, ObjectFile* object, u64 image_base,
                                                        u64 const* section_offsets, u64 header_offset)
{
    u64 header_size = table->count ? 12 + (u64)table->count * 8 : 0;
    if (!table->valid || !header_size || header_offset > byte_count || header_size > byte_count - header_offset)
    {
        return !header_size && table->valid;
    }
    u64 header_address = image_base + header_offset;
    for (u32 entry_index = 0; entry_index < table->count; entry_index += 1)
    {
        LinkElfEhFrameEntry* entry = table->entries + entry_index;
        ObjectRelocation* relocation = object->relocations + entry->relocation;
        ObjectSymbol* symbol = object->symbols + relocation->symbol;
        if (symbol->section >= OBJECT_SECTION_COUNT)
        {
            return false;
        }
        u64 address = image_base + section_offsets[symbol->section] + symbol->value;
        if (relocation->addend >= 0)
        {
            if ((u64)relocation->addend > UINT64_MAX - address)
            {
                return false;
            }
            address += (u64)relocation->addend;
        }
        else
        {
            u64 magnitude = (u64)(-(relocation->addend + 1)) + 1;
            if (magnitude > address)
            {
                return false;
            }
            address -= magnitude;
        }
        entry->function_address = address;
    }
    LinkElfEhFrameEntry* source = table->entries;
    LinkElfEhFrameEntry* destination = table->scratch;
    for (u32 byte_index = 0; byte_index < 8; byte_index += 1)
    {
        u32 counts[256] = {0};
        u32 offsets[256];
        u32 shift = byte_index * 8;
        for (u32 entry_index = 0; entry_index < table->count; entry_index += 1)
        {
            counts[(source[entry_index].function_address >> shift) & 0xff] += 1;
        }
        u32 offset = 0;
        for (u32 bucket = 0; bucket < BUSTER_ARRAY_LENGTH(counts); bucket += 1)
        {
            offsets[bucket] = offset;
            offset += counts[bucket];
        }
        for (u32 entry_index = 0; entry_index < table->count; entry_index += 1)
        {
            u32 bucket = (source[entry_index].function_address >> shift) & 0xff;
            destination[offsets[bucket]++] = source[entry_index];
        }
        LinkElfEhFrameEntry* swap = source;
        source = destination;
        destination = swap;
    }
    table->entries = source;
    bytes[header_offset] = 1;
    bytes[header_offset + 1] = 0x1b;
    bytes[header_offset + 2] = 0x03;
    bytes[header_offset + 3] = 0x3b;
    s32 relative = 0;
    u64 unwind_address = image_base + section_offsets[OBJECT_SECTION_UNWIND];
    if (!link_elf_signed_difference(unwind_address, header_address + 4, &relative))
    {
        return false;
    }
    link_write_u32(bytes, header_offset + 4, (u32)relative);
    link_write_u32(bytes, header_offset + 8, table->count);
    for (u32 entry_index = 0; entry_index < table->count; entry_index += 1)
    {
        LinkElfEhFrameEntry* entry = table->entries + entry_index;
        u64 output = header_offset + 12 + (u64)entry_index * 8;
        u64 fde_address = unwind_address + entry->fde_offset;
        if (!link_elf_signed_difference(entry->function_address, header_address, &relative))
        {
            return false;
        }
        link_write_u32(bytes, output, (u32)relative);
        if (!link_elf_signed_difference(fde_address, header_address, &relative))
        {
            return false;
        }
        link_write_u32(bytes, output + 4, (u32)relative);
    }
    return true;
}

typedef struct LinkElfSectionDescriptor LinkElfSectionDescriptor;
struct LinkElfSectionDescriptor
{
    String8 name;
    u64 flags;
    u64 address;
    u64 offset;
    u64 size;
    u64 alignment;
    u64 entry_size;
    u32 type;
    u32 link;
    u32 info;
    u32 reserved;
};

// Appends merged DWARF data and an ELF section table to a finished image.
// Loaded bytes retain their program-header layout, while zero-fill and debug
// sections preserve their virtual and non-loaded representations. Debug
// relocations are resolved statically here: 64-bit slots receive link-time
// addresses and 32-bit slots receive offsets into the target debug section.
BUSTER_GLOBAL_LOCAL void link_elf_section_table_append(Arena* arena, NativeExecutableLinkResult* result, ObjectFile* object, u64 image_base,
                                                       u64 const* section_offsets, LinkElfSectionTableLayout layout)
{
    if (result->error != LINK_ERROR_NONE || object->section_count < OBJECT_SECTION_COUNT)
    {
        return;
    }
    LinkElfSectionDescriptor descriptors[BUSTER_LINK_ELF_SECTION_DESCRIPTOR_CAPACITY] = {0};
    u32 descriptor_count = 0;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(link_elf_loaded_kinds); index += 1)
    {
        ObjectSectionKind kind = link_elf_loaded_kinds[index];
        ObjectSection* section = &object->sections[kind];
        u64 size = BUSTER_MAX(section->data.length, section->virtual_size);
        u64 offset = section_offsets[kind];
        bool thread_local = kind == OBJECT_SECTION_THREAD_LOCAL_DATA || kind == OBJECT_SECTION_THREAD_LOCAL_ZERO;
        bool writable = kind == OBJECT_SECTION_DATA || kind == OBJECT_SECTION_ZERO || thread_local;
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = object_section_name_for_kind(kind),
            .flags = (kind == OBJECT_SECTION_TEXT ? UINT64_C(0x6) : writable ? UINT64_C(0x3) : UINT64_C(0x2)) |
                     (thread_local ? UINT64_C(0x400) : 0),
            .address = size ? image_base + offset : 0,
            .offset = size ? offset : 0,
            .size = size,
            .alignment = section->alignment,
            .type = object_section_kind_is_zero_fill(kind) ? 8 : 1,
        };
    }
    if (layout.eh_frame_header_size)
    {
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".eh_frame_hdr"),
            .flags = 0x2,
            .address = image_base + layout.eh_frame_header_offset,
            .offset = layout.eh_frame_header_offset,
            .size = layout.eh_frame_header_size,
            .alignment = 4,
            .type = 1,
        };
    }
    if (layout.dynamic)
    {
        u32 dynamic_base = 1 + descriptor_count;
        u32 dynamic_string_index = dynamic_base + 2;
        u32 dynamic_symbol_index = dynamic_base + 3;
        u32 got_index = dynamic_base + 6;
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".interp"),
            .flags = 0x2,
            .address = image_base + layout.interpreter_offset,
            .offset = layout.interpreter_offset,
            .size = layout.interpreter_size,
            .alignment = 1,
            .type = 1,
        };
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".plt"),
            .flags = 0x6,
            .address = image_base + layout.plt_offset,
            .offset = layout.plt_offset,
            .size = layout.plt_size,
            .alignment = 16,
            .entry_size = 16,
            .type = 1,
        };
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".dynstr"),
            .flags = 0x2,
            .address = image_base + layout.dynamic_string_offset,
            .offset = layout.dynamic_string_offset,
            .size = layout.dynamic_string_size,
            .alignment = 1,
            .type = 3,
        };
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".dynsym"),
            .flags = 0x2,
            .address = image_base + layout.dynamic_symbol_offset,
            .offset = layout.dynamic_symbol_offset,
            .size = layout.dynamic_symbol_size,
            .alignment = 8,
            .entry_size = 24,
            .type = 11,
            .link = dynamic_string_index,
        };
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".hash"),
            .flags = 0x2,
            .address = image_base + layout.hash_offset,
            .offset = layout.hash_offset,
            .size = layout.hash_size,
            .alignment = 4,
            .entry_size = 4,
            .type = 5,
            .link = dynamic_symbol_index,
        };
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".rela.plt"),
            .flags = 0x2,
            .address = image_base + layout.relocation_offset,
            .offset = layout.relocation_offset,
            .size = layout.relocation_size,
            .alignment = 8,
            .entry_size = 24,
            .type = 4,
            .link = dynamic_symbol_index,
            .info = got_index,
        };
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".got.plt"),
            .flags = 0x3,
            .address = image_base + layout.got_offset,
            .offset = layout.got_offset,
            .size = layout.got_size,
            .alignment = 8,
            .entry_size = 8,
            .type = 1,
        };
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = S8(".dynamic"),
            .flags = 0x3,
            .address = image_base + layout.dynamic_offset,
            .offset = layout.dynamic_offset,
            .size = layout.dynamic_size,
            .alignment = 8,
            .entry_size = 16,
            .type = 6,
            .link = dynamic_string_index,
        };
    }
    u64 debug_offsets[BUSTER_ARRAY_LENGTH(link_elf_debug_kinds)];
    u64 cursor = result->executable.length;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(link_elf_debug_kinds); index += 1)
    {
        ObjectSection* section = &object->sections[link_elf_debug_kinds[index]];
        debug_offsets[index] = cursor;
        if (section->data.length > UINT64_MAX - cursor)
        {
            result->error = LINK_ERROR_INVALID_INPUT;
            return;
        }
        descriptors[descriptor_count++] = (LinkElfSectionDescriptor){
            .name = object_section_name_for_kind(link_elf_debug_kinds[index]),
            .offset = cursor,
            .size = section->data.length,
            .alignment = section->alignment,
            .type = 1,
        };
        cursor += section->data.length;
    }
    u64 string_offset = cursor;
    u64 name_offsets[BUSTER_LINK_ELF_SECTION_DESCRIPTOR_CAPACITY + 2] = {0};
    u64 string_size = 1;
    for (u32 index = 0; index < descriptor_count; index += 1)
    {
        name_offsets[index + 1] = string_size;
        if (descriptors[index].name.length > UINT64_MAX - string_size - 1)
        {
            result->error = LINK_ERROR_INVALID_INPUT;
            return;
        }
        string_size += descriptors[index].name.length + 1;
    }
    u32 string_table_index = descriptor_count + 1;
    u32 section_count = string_table_index + 1;
    name_offsets[string_table_index] = string_size;
    string_size += sizeof(".shstrtab");
    if (string_size > UINT64_MAX - string_offset)
    {
        result->error = LINK_ERROR_INVALID_INPUT;
        return;
    }
    cursor = align_forward(string_offset + string_size, 8);
    u64 header_offset = cursor;
    if ((u64)section_count > (UINT64_MAX - header_offset) / BUSTER_LINK_ELF_SECTION_HEADER_SIZE)
    {
        result->error = LINK_ERROR_INVALID_INPUT;
        return;
    }
    u64 total_size = header_offset + (u64)section_count * BUSTER_LINK_ELF_SECTION_HEADER_SIZE;
    u8* bytes = arena_allocate(arena, u8, total_size);
    memcpy(bytes, result->executable.pointer, result->executable.length);
    memset(bytes + result->executable.length, 0, total_size - result->executable.length);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(link_elf_debug_kinds); index += 1)
    {
        ByteSlice data = object->sections[link_elf_debug_kinds[index]].data;
        if (data.length)
        {
            memcpy(bytes + debug_offsets[index], data.pointer, data.length);
        }
    }
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[index];
        if (relocation->section >= OBJECT_SECTION_COUNT || !object_section_kind_is_debug((ObjectSectionKind)relocation->section) ||
            relocation->symbol >= object->symbol_count)
        {
            continue;
        }
        u32 debug_index = 0;
        while (debug_index < BUSTER_ARRAY_LENGTH(link_elf_debug_kinds) && link_elf_debug_kinds[debug_index] != (ObjectSectionKind)relocation->section)
        {
            debug_index += 1;
        }
        if (debug_index >= BUSTER_ARRAY_LENGTH(link_elf_debug_kinds))
        {
            continue;
        }
        ObjectSection* section = &object->sections[relocation->section];
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        u64 width = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (relocation->offset > section->data.length || width > section->data.length - relocation->offset || symbol->section >= OBJECT_SECTION_COUNT)
        {
            result->error = LINK_ERROR_RELOCATION;
            return;
        }
        u64 slot = debug_offsets[debug_index] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            // DWARF 4 range and location lists use offset pairs relative to
            // the containing compilation unit's base address.  Their text
            // relocations therefore remain section-relative after linking;
            // adding the final image address here makes consumers add the CU
            // base a second time.  Other debug address attributes (line
            // addresses, DW_OP_addr, and DW_AT_low_pc) are true addresses.
            bool relative_text_range = (relocation->section == OBJECT_SECTION_DEBUG_LOC || relocation->section == OBJECT_SECTION_DEBUG_RANGES) &&
                                       symbol->section == OBJECT_SECTION_TEXT;
            s64 value = (s64)symbol->value + relocation->addend;
            if (!relative_text_range)
            {
                value += (s64)image_base + (s64)section_offsets[symbol->section];
            }
            link_write_u64(bytes, slot, (u64)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE32 && object_section_kind_is_debug((ObjectSectionKind)symbol->section))
        {
            link_write_u32(bytes, slot, (u32)((s64)symbol->value + relocation->addend));
        }
        else
        {
            result->error = LINK_ERROR_RELOCATION;
            return;
        }
    }
    u64 name_cursor = string_offset + 1;
    for (u32 index = 0; index < descriptor_count; index += 1)
    {
        String8 name = descriptors[index].name;
        memcpy(bytes + name_cursor, name.pointer, name.length);
        name_cursor += name.length + 1;
    }
    memcpy(bytes + name_cursor, ".shstrtab", sizeof(".shstrtab"));
    for (u32 descriptor_index = 0; descriptor_index < descriptor_count; descriptor_index += 1)
    {
        u32 header_index = descriptor_index + 1;
        u64 offset = header_offset + (u64)header_index * BUSTER_LINK_ELF_SECTION_HEADER_SIZE;
        link_write_u32(bytes, offset, (u32)name_offsets[header_index]);
        LinkElfSectionDescriptor* descriptor = &descriptors[descriptor_index];
        link_write_u32(bytes, offset + 4, descriptor->type);
        link_write_u64(bytes, offset + 8, descriptor->flags);
        link_write_u64(bytes, offset + 16, descriptor->address);
        link_write_u64(bytes, offset + 24, descriptor->offset);
        link_write_u64(bytes, offset + 32, descriptor->size);
        link_write_u32(bytes, offset + 40, descriptor->link);
        link_write_u32(bytes, offset + 44, descriptor->info);
        link_write_u64(bytes, offset + 48, descriptor->alignment);
        link_write_u64(bytes, offset + 56, descriptor->entry_size);
    }
    u64 string_header = header_offset + (u64)string_table_index * BUSTER_LINK_ELF_SECTION_HEADER_SIZE;
    link_write_u32(bytes, string_header, (u32)name_offsets[string_table_index]);
    link_write_u32(bytes, string_header + 4, 3);
    link_write_u64(bytes, string_header + 24, string_offset);
    link_write_u64(bytes, string_header + 32, string_size);
    link_write_u64(bytes, string_header + 48, 1);
    link_write_u64(bytes, 40, header_offset);
    link_write_u16(bytes, 58, BUSTER_LINK_ELF_SECTION_HEADER_SIZE);
    link_write_u16(bytes, 60, (u16)section_count);
    link_write_u16(bytes, 62, (u16)string_table_index);
    result->executable = (ByteSlice){
        .pointer = bytes,
        .length = total_size,
    };
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult link_native_executable_elf64_x86_64(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_PAGE_SIZE = 4096,
        ELF_MACHINE_X86_64 = 62,
    };
    static u8 const entry_stub[] = {
        0x31, 0xed, 0x48, 0x8b, 0x3c, 0x24, 0x48, 0x8d, 0x74, 0x24, 0x08, 0x48, 0x8d, 0x54, 0xfe, 0x08, 0x48, 0x83,
        0xe4, 0xf0, 0xe8, 0,    0,    0,    0,    0x89, 0xc7, 0xb8, 0x3c, 0,    0,    0,    0x0f, 0x05, 0xf4,
    };
    if ((options.dynamic_library_count && !options.dynamic_libraries) || (options.runtime_exported_symbol_count && !options.runtime_exported_symbols) ||
        object->section_count < OBJECT_SECTION_COUNT || !object->sections || (object->symbol_count && !object->symbols) ||
        (object->relocation_count && !object->relocations))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        if (object->symbols[symbol_index].section == OBJECT_SECTION_UNDEFINED)
        {
            result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = object->symbols[symbol_index].name;
            return result;
        }
    }
    String8 entry_name = options.entry_symbol.length ? options.entry_symbol : S8("main");
    u32 entry_symbol_index = link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX || object->symbols[entry_symbol_index].section == OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind != OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    LinkElfEhFrameTable eh_frame_table = link_elf_eh_frame_table_build(arena, object);
    if (!eh_frame_table.valid)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    u64 eh_frame_header_size = eh_frame_table.count ? 12 + (u64)eh_frame_table.count * 8 : 0;
    bool has_writable = object->sections[OBJECT_SECTION_DATA].data.length || object->sections[OBJECT_SECTION_ZERO].virtual_size;
    u32 load_program_header_count = has_writable ? 2 : 1;
    u32 program_header_count = load_program_header_count + (eh_frame_header_size != 0);
    u64 header_end = ELF_HEADER_SIZE + (u64)program_header_count * ELF_PROGRAM_HEADER_SIZE;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 entry_stub_offset = align_forward(header_end, 16);
    section_offsets[OBJECT_SECTION_TEXT] = align_forward(entry_stub_offset + sizeof(entry_stub), object->sections[OBJECT_SECTION_TEXT].alignment);
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] = align_forward(section_offsets[OBJECT_SECTION_TEXT] + object->sections[OBJECT_SECTION_TEXT].data.length,
                                                                   object->sections[OBJECT_SECTION_READ_ONLY_DATA].alignment);
    u64 eh_frame_header_offset = align_forward(section_offsets[OBJECT_SECTION_READ_ONLY_DATA] + object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length, 4);
    section_offsets[OBJECT_SECTION_UNWIND] = align_forward(eh_frame_header_offset + eh_frame_header_size, object->sections[OBJECT_SECTION_UNWIND].alignment);
    u64 read_only_end = section_offsets[OBJECT_SECTION_UNWIND] + object->sections[OBJECT_SECTION_UNWIND].data.length;
    section_offsets[OBJECT_SECTION_DATA] = align_forward(read_only_end, ELF_PAGE_SIZE);
    u64 file_size = object->sections[OBJECT_SECTION_DATA].data.length ? section_offsets[OBJECT_SECTION_DATA] + object->sections[OBJECT_SECTION_DATA].data.length
                                                                      : read_only_end;
    section_offsets[OBJECT_SECTION_ZERO] = align_forward(BUSTER_MAX(file_size, section_offsets[OBJECT_SECTION_DATA]), object->sections[OBJECT_SECTION_ZERO].alignment);
    u64 writable_memory_end = section_offsets[OBJECT_SECTION_ZERO] + object->sections[OBJECT_SECTION_ZERO].virtual_size;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(arena, u8, file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(bytes + entry_stub_offset, entry_stub, sizeof(entry_stub));
    for (u32 section = 0; section < OBJECT_SECTION_COUNT; section += 1)
    {
        if (object_section_kind_is_debug((ObjectSectionKind)section))
        {
            continue;
        }
        ByteSlice data = object->sections[section].data;
        if (data.length)
        {
            memcpy(bytes + section_offsets[section], data.pointer, data.length);
        }
    }
    u64 image_base = 0x400000;
    ObjectSymbol* entry_symbol = &object->symbols[entry_symbol_index];
    u64 entry_address = image_base + section_offsets[entry_symbol->section] + entry_symbol->value;
    u64 call_displacement_offset = entry_stub_offset + 21;
    s64 call_displacement = (s64)entry_address - (s64)(image_base + call_displacement_offset + 4);
    if (call_displacement < INT32_MIN || call_displacement > INT32_MAX)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(bytes, call_displacement_offset, (u32)(s32)call_displacement);
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[index];
        if (relocation->section < OBJECT_SECTION_COUNT && object_section_kind_is_debug((ObjectSectionKind)relocation->section))
        {
            continue;
        }
        if (relocation->section >= OBJECT_SECTION_COUNT || relocation->symbol >= object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section = &object->sections[relocation->section];
        u64 width = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (relocation->offset > section->data.length || width > section->data.length - relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        if (symbol->section >= OBJECT_SECTION_COUNT)
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 symbol_address = image_base + section_offsets[symbol->section] + symbol->value;
        u64 place_address = image_base + section_offsets[relocation->section] + relocation->offset;
        u64 output_offset = section_offsets[relocation->section] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 value = (s64)symbol_address + relocation->addend - (s64)place_address;
            if (value < INT32_MIN || value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)(s32)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            link_write_u64(bytes, output_offset, symbol_address + (u64)relocation->addend);
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    if (!link_elf_eh_frame_header_write(bytes, file_size, &eh_frame_table, object, image_base, section_offsets, eh_frame_header_offset))
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    link_write_u16(bytes, 16, 2);
    link_write_u16(bytes, 18, ELF_MACHINE_X86_64);
    link_write_u32(bytes, 20, 1);
    link_write_u64(bytes, 24, image_base + entry_stub_offset);
    link_write_u64(bytes, 32, ELF_HEADER_SIZE);
    link_write_u16(bytes, 52, ELF_HEADER_SIZE);
    link_write_u16(bytes, 54, ELF_PROGRAM_HEADER_SIZE);
    link_write_u16(bytes, 56, (u16)program_header_count);
    u64 program_header = ELF_HEADER_SIZE;
    link_write_u32(bytes, program_header, 1);
    link_write_u32(bytes, program_header + 4, 5);
    link_write_u64(bytes, program_header + 16, image_base);
    link_write_u64(bytes, program_header + 24, image_base);
    link_write_u64(bytes, program_header + 32, read_only_end);
    link_write_u64(bytes, program_header + 40, read_only_end);
    link_write_u64(bytes, program_header + 48, ELF_PAGE_SIZE);
    if (load_program_header_count == 2)
    {
        program_header += ELF_PROGRAM_HEADER_SIZE;
        u64 data_size = object->sections[OBJECT_SECTION_DATA].data.length;
        u64 writable_file_size = file_size > section_offsets[OBJECT_SECTION_DATA] ? file_size - section_offsets[OBJECT_SECTION_DATA] : 0;
        u64 writable_memory_size = writable_memory_end - section_offsets[OBJECT_SECTION_DATA];
        link_write_u32(bytes, program_header, 1);
        link_write_u32(bytes, program_header + 4, 6);
        link_write_u64(bytes, program_header + 8, section_offsets[OBJECT_SECTION_DATA]);
        link_write_u64(bytes, program_header + 16, image_base + section_offsets[OBJECT_SECTION_DATA]);
        link_write_u64(bytes, program_header + 24, image_base + section_offsets[OBJECT_SECTION_DATA]);
        link_write_u64(bytes, program_header + 32, writable_file_size);
        link_write_u64(bytes, program_header + 40, BUSTER_MAX(data_size, writable_memory_size));
        link_write_u64(bytes, program_header + 48, ELF_PAGE_SIZE);
    }
    if (eh_frame_header_size)
    {
        program_header = ELF_HEADER_SIZE + (u64)load_program_header_count * ELF_PROGRAM_HEADER_SIZE;
        link_write_u32(bytes, program_header, 0x6474e550);
        link_write_u32(bytes, program_header + 4, 4);
        link_write_u64(bytes, program_header + 8, eh_frame_header_offset);
        link_write_u64(bytes, program_header + 16, image_base + eh_frame_header_offset);
        link_write_u64(bytes, program_header + 24, image_base + eh_frame_header_offset);
        link_write_u64(bytes, program_header + 32, eh_frame_header_size);
        link_write_u64(bytes, program_header + 40, eh_frame_header_size);
        link_write_u64(bytes, program_header + 48, 4);
    }
    link_elf_section_table_append(arena, &result, object, image_base, section_offsets,
                                  (LinkElfSectionTableLayout){
                                      .eh_frame_header_offset = eh_frame_header_offset,
                                      .eh_frame_header_size = eh_frame_header_size,
                                  });
    if (options.output_path.length && !link_write_executable_file(options.output_path, result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult link_native_executable_elf64_x86_64_dynamic(Arena* arena, ObjectFile* object,
                                                                                           NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_BASE_PROGRAM_HEADER_COUNT = 7,
        ELF_PAGE_SIZE = 4096,
        ELF_MACHINE_X86_64 = 62,
        ELF_SYMBOL_SIZE = 24,
        ELF_RELOCATION_SIZE = 24,
        ELF_DYNAMIC_SIZE = 16,
        ELF_PLT_ENTRY_SIZE = 16,
        ELF_GOT_RESERVED_COUNT = 3,
    };
    static u8 const entry_stub[] = {
        0x31, 0xed, 0x48, 0x8b, 0x3c, 0x24, 0x48, 0x8d, 0x74, 0x24, 0x08, 0x48, 0x8d, 0x54, 0xfe, 0x08, 0x48, 0x83,
        0xe4, 0xf0, 0xe8, 0,    0,    0,    0,    0x89, 0xc7, 0xb8, 0x3c, 0,    0,    0,    0x0f, 0x05, 0xf4,
    };
    static char8 const interpreter[] = "/lib64/ld-linux-x86-64.so.2";
    static char8 const library_name[] = "libc.so.6";
    if ((options.dynamic_library_count && !options.dynamic_libraries) || object->section_count < OBJECT_SECTION_COUNT || !object->sections ||
        (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u32 import_count = 0;
    u64 imported_name_size = 0;
    u32* import_indices = arena_allocate(arena, u32, object->symbol_count);
    u32* import_name_offsets = arena_allocate(arena, u32, object->symbol_count);
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        ObjectSymbol* symbol = &object->symbols[symbol_index];
        if (symbol->section != OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        if (!symbol->global || symbol->kind != OBJECT_SYMBOL_FUNCTION || !symbol->name.length || symbol->name.length > UINT32_MAX ||
            imported_name_size > UINT32_MAX - symbol->name.length - 1)
        {
            result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = symbol->name;
            return result;
        }
        import_indices[symbol_index] = import_count;
        imported_name_size += symbol->name.length + 1;
        import_count += 1;
    }
    bool has_thread_local_data =
        object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length != 0 || object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size != 0;
    if (!import_count && !has_thread_local_data && !options.dynamic_library_count)
    {
        return link_native_executable_elf64_x86_64(arena, object, options);
    }
    String8 entry_name = options.entry_symbol.length ? options.entry_symbol : S8("main");
    u32 entry_symbol_index = link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX || object->symbols[entry_symbol_index].section == OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind != OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    LinkElfEhFrameTable eh_frame_table = link_elf_eh_frame_table_build(arena, object);
    if (!eh_frame_table.valid)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    u64 eh_frame_header_size = eh_frame_table.count ? 12 + (u64)eh_frame_table.count * 8 : 0;
    u32 program_header_count = ELF_BASE_PROGRAM_HEADER_COUNT + (eh_frame_header_size != 0);
    u64 header_end = ELF_HEADER_SIZE + (u64)program_header_count * ELF_PROGRAM_HEADER_SIZE;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 entry_stub_offset = align_forward(header_end, 16);
    section_offsets[OBJECT_SECTION_TEXT] = align_forward(entry_stub_offset + sizeof(entry_stub), object->sections[OBJECT_SECTION_TEXT].alignment);
    u64 plt_offset = align_forward(section_offsets[OBJECT_SECTION_TEXT] + object->sections[OBJECT_SECTION_TEXT].data.length, 16);
    u64 plt_size = (u64)(import_count + 1) * ELF_PLT_ENTRY_SIZE;
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] = align_forward(plt_offset + plt_size, object->sections[OBJECT_SECTION_READ_ONLY_DATA].alignment);
    u64 eh_frame_header_offset = align_forward(section_offsets[OBJECT_SECTION_READ_ONLY_DATA] + object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length, 4);
    section_offsets[OBJECT_SECTION_UNWIND] = align_forward(eh_frame_header_offset + eh_frame_header_size, object->sections[OBJECT_SECTION_UNWIND].alignment);
    u64 interpreter_offset = section_offsets[OBJECT_SECTION_UNWIND] + object->sections[OBJECT_SECTION_UNWIND].data.length;
    u64 interpreter_size = sizeof(interpreter);
    if (options.dynamic_library_count == UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u32 needed_library_count = options.dynamic_library_count + 1;
    u32* library_name_offsets = arena_allocate(arena, u32, needed_library_count);
    u64 library_name_size = sizeof(library_name);
    library_name_offsets[0] = 1;
    for (u32 library_index = 0; library_index < options.dynamic_library_count; library_index += 1)
    {
        String8 library = options.dynamic_libraries[library_index].name;
        if (!library.length || library.length >= UINT32_MAX || library_name_size > UINT32_MAX - library.length - 1)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        library_name_offsets[library_index + 1] = 1 + (u32)library_name_size;
        library_name_size += library.length + 1;
    }
    u64 dynamic_string_offset = interpreter_offset + interpreter_size;
    u32 first_symbol_name_offset = 1 + (u32)library_name_size;
    u64 dynamic_string_size = 1 + library_name_size + imported_name_size;
    u64 dynamic_symbol_offset = align_forward(dynamic_string_offset + dynamic_string_size, 8);
    u64 dynamic_symbol_size = (u64)(import_count + 1) * ELF_SYMBOL_SIZE;
    u64 hash_offset = align_forward(dynamic_symbol_offset + dynamic_symbol_size, 4);
    u64 hash_size = (u64)(2 + 1 + import_count + 1) * sizeof(u32);
    u64 relocation_offset = align_forward(hash_offset + hash_size, 8);
    u64 relocation_size = (u64)import_count * ELF_RELOCATION_SIZE;
    u64 read_only_end = relocation_offset + relocation_size;
    section_offsets[OBJECT_SECTION_DATA] = align_forward(read_only_end, ELF_PAGE_SIZE);
    u64 got_offset = align_forward(section_offsets[OBJECT_SECTION_DATA] + object->sections[OBJECT_SECTION_DATA].data.length, 8);
    u64 got_size = (u64)(ELF_GOT_RESERVED_COUNT + import_count) * sizeof(u64);
    u64 dynamic_offset = align_forward(got_offset + got_size, 8);
    u32 dynamic_count = needed_library_count + 11;
    u64 dynamic_size = (u64)dynamic_count * ELF_DYNAMIC_SIZE;
    section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA] = align_forward(dynamic_offset + dynamic_size, object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].alignment);
    u64 file_size = section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA] + object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length;
    section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO] = align_forward(file_size, object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment);
    u64 thread_local_memory_end = section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO] + object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size;
    section_offsets[OBJECT_SECTION_ZERO] = align_forward(thread_local_memory_end, object->sections[OBJECT_SECTION_ZERO].alignment);
    u64 writable_memory_end = section_offsets[OBJECT_SECTION_ZERO] + object->sections[OBJECT_SECTION_ZERO].virtual_size;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(arena, u8, file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(bytes + entry_stub_offset, entry_stub, sizeof(entry_stub));
    for (u32 section = 0; section < OBJECT_SECTION_COUNT; section += 1)
    {
        if (object_section_kind_is_debug((ObjectSectionKind)section))
        {
            continue;
        }
        ByteSlice data = object->sections[section].data;
        if (data.length)
        {
            memcpy(bytes + section_offsets[section], data.pointer, data.length);
        }
    }
    memcpy(bytes + interpreter_offset, interpreter, interpreter_size);
    memcpy(bytes + dynamic_string_offset + library_name_offsets[0], library_name, sizeof(library_name));
    for (u32 library_index = 0; library_index < options.dynamic_library_count; library_index += 1)
    {
        String8 library = options.dynamic_libraries[library_index].name;
        u64 output = dynamic_string_offset + library_name_offsets[library_index + 1];
        memcpy(bytes + output, library.pointer, library.length);
        bytes[output + library.length] = 0;
    }
    u64 dynamic_name_cursor = dynamic_string_offset + first_symbol_name_offset;
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        u32 import_index = import_indices[symbol_index];
        if (import_index == UINT32_MAX)
        {
            continue;
        }
        ObjectSymbol* symbol = &object->symbols[symbol_index];
        import_name_offsets[import_index] = (u32)(dynamic_name_cursor - dynamic_string_offset);
        memcpy(bytes + dynamic_name_cursor, symbol->name.pointer, symbol->name.length);
        dynamic_name_cursor += symbol->name.length + 1;
        u64 symbol_offset = dynamic_symbol_offset + (u64)(import_index + 1) * ELF_SYMBOL_SIZE;
        link_write_u32(bytes, symbol_offset, import_name_offsets[import_index]);
        bytes[symbol_offset + 4] = 0x12;
    }
    link_write_u32(bytes, hash_offset, 1);
    link_write_u32(bytes, hash_offset + 4, import_count + 1);
    link_write_u32(bytes, hash_offset + 8, 1);
    for (u32 import_index = 1; import_index <= import_count; import_index += 1)
    {
        link_write_u32(bytes, hash_offset + (u64)(3 + import_index) * sizeof(u32), import_index == import_count ? 0 : import_index + 1);
    }
    u64 image_base = 0x400000;
    u64 got_address = image_base + got_offset;
    u64 plt_address = image_base + plt_offset;
    u64 dynamic_address = image_base + dynamic_offset;
    bytes[plt_offset] = 0xff;
    bytes[plt_offset + 1] = 0x35;
    link_write_u32(bytes, plt_offset + 2, (u32)(s32)((s64)(got_address + 8) - (s64)(plt_address + 6)));
    bytes[plt_offset + 6] = 0xff;
    bytes[plt_offset + 7] = 0x25;
    link_write_u32(bytes, plt_offset + 8, (u32)(s32)((s64)(got_address + 16) - (s64)(plt_address + 12)));
    bytes[plt_offset + 12] = 0x0f;
    bytes[plt_offset + 13] = 0x1f;
    bytes[plt_offset + 14] = 0x40;
    link_write_u64(bytes, got_offset, dynamic_address);
    for (u32 import_index = 0; import_index < import_count; import_index += 1)
    {
        u64 entry_offset = plt_offset + (u64)(import_index + 1) * ELF_PLT_ENTRY_SIZE;
        u64 entry_address = image_base + entry_offset;
        u64 slot_offset = got_offset + (u64)(ELF_GOT_RESERVED_COUNT + import_index) * sizeof(u64);
        u64 slot_address = image_base + slot_offset;
        bytes[entry_offset] = 0xff;
        bytes[entry_offset + 1] = 0x25;
        link_write_u32(bytes, entry_offset + 2, (u32)(s32)((s64)slot_address - (s64)(entry_address + 6)));
        bytes[entry_offset + 6] = 0x68;
        link_write_u32(bytes, entry_offset + 7, import_index);
        bytes[entry_offset + 11] = 0xe9;
        link_write_u32(bytes, entry_offset + 12, (u32)(s32)((s64)plt_address - (s64)(entry_address + 16)));
        link_write_u64(bytes, slot_offset, entry_address + 6);
        u64 relocation_entry = relocation_offset + (u64)import_index * ELF_RELOCATION_SIZE;
        link_write_u64(bytes, relocation_entry, slot_address);
        link_write_u64(bytes, relocation_entry + 8, ((u64)(import_index + 1) << 32) | 7);
    }
    ObjectSymbol* entry_symbol = &object->symbols[entry_symbol_index];
    u64 entry_address = image_base + section_offsets[entry_symbol->section] + entry_symbol->value;
    u64 call_displacement_offset = entry_stub_offset + 21;
    s64 call_displacement = (s64)entry_address - (s64)(image_base + call_displacement_offset + 4);
    if (call_displacement < INT32_MIN || call_displacement > INT32_MAX)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(bytes, call_displacement_offset, (u32)(s32)call_displacement);
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[index];
        if (relocation->section < OBJECT_SECTION_COUNT && object_section_kind_is_debug((ObjectSectionKind)relocation->section))
        {
            continue;
        }
        if (relocation->section >= OBJECT_SECTION_COUNT || relocation->symbol >= object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section = &object->sections[relocation->section];
        u64 width = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (relocation->offset > section->data.length || width > section->data.length - relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        u64 symbol_address = 0;
        if (symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index = import_indices[relocation->symbol];
            if (import_index == UINT32_MAX || relocation->kind != OBJECT_RELOCATION_X86_64_PC32)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_address = plt_address + (u64)(import_index + 1) * ELF_PLT_ENTRY_SIZE;
        }
        else if (symbol->section < OBJECT_SECTION_COUNT)
        {
            symbol_address = image_base + section_offsets[symbol->section] + symbol->value;
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 place_address = image_base + section_offsets[relocation->section] + relocation->offset;
        u64 output_offset = section_offsets[relocation->section] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 value = (s64)symbol_address + relocation->addend - (s64)place_address;
            if (value < INT32_MIN || value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)(s32)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            link_write_u64(bytes, output_offset, symbol_address + (u64)relocation->addend);
        }
        else if (relocation->kind == OBJECT_RELOCATION_X86_64_TPOFF32)
        {
            if (symbol->section != OBJECT_SECTION_THREAD_LOCAL_DATA && symbol->section != OBJECT_SECTION_THREAD_LOCAL_ZERO)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            u64 initialized_size =
                align_forward(object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length, object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment);
            u64 thread_local_size = align_forward(initialized_size + object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size,
                                                  object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].alignment);
            u64 symbol_offset = symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO ? initialized_size + symbol->value : symbol->value;
            s64 value = (s64)symbol_offset + relocation->addend - (s64)thread_local_size;
            if (value < INT32_MIN || value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)(s32)value);
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    if (!link_elf_eh_frame_header_write(bytes, file_size, &eh_frame_table, object, image_base, section_offsets, eh_frame_header_offset))
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    u64 dynamic_cursor = dynamic_offset;
#define BUSTER_LINK_DYNAMIC(tag, value)                                                                                                                        \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        link_write_u64(bytes, dynamic_cursor, (tag));                                                                                                          \
        link_write_u64(bytes, dynamic_cursor + 8, (value));                                                                                                    \
        dynamic_cursor += ELF_DYNAMIC_SIZE;                                                                                                                    \
    } while (0)
    for (u32 library_index = 0; library_index < needed_library_count; library_index += 1)
    {
        BUSTER_LINK_DYNAMIC(1, library_name_offsets[library_index]);
    }
    BUSTER_LINK_DYNAMIC(4, image_base + hash_offset);
    BUSTER_LINK_DYNAMIC(5, image_base + dynamic_string_offset);
    BUSTER_LINK_DYNAMIC(6, image_base + dynamic_symbol_offset);
    BUSTER_LINK_DYNAMIC(10, dynamic_string_size);
    BUSTER_LINK_DYNAMIC(11, ELF_SYMBOL_SIZE);
    BUSTER_LINK_DYNAMIC(3, got_address);
    BUSTER_LINK_DYNAMIC(2, relocation_size);
    BUSTER_LINK_DYNAMIC(20, 7);
    BUSTER_LINK_DYNAMIC(23, image_base + relocation_offset);
    BUSTER_LINK_DYNAMIC(9, ELF_RELOCATION_SIZE);
    BUSTER_LINK_DYNAMIC(0, 0);
#undef BUSTER_LINK_DYNAMIC
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    link_write_u16(bytes, 16, 2);
    link_write_u16(bytes, 18, ELF_MACHINE_X86_64);
    link_write_u32(bytes, 20, 1);
    link_write_u64(bytes, 24, image_base + entry_stub_offset);
    link_write_u64(bytes, 32, ELF_HEADER_SIZE);
    link_write_u16(bytes, 52, ELF_HEADER_SIZE);
    link_write_u16(bytes, 54, ELF_PROGRAM_HEADER_SIZE);
    link_write_u16(bytes, 56, (u16)program_header_count);
    u64 program_header = ELF_HEADER_SIZE;
#define BUSTER_LINK_PROGRAM_HEADER(type, flags, offset, address, file_bytes, memory_bytes, alignment)                                                         \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        link_write_u32(bytes, program_header, (type));                                                                                                         \
        link_write_u32(bytes, program_header + 4, (flags));                                                                                                    \
        link_write_u64(bytes, program_header + 8, (offset));                                                                                                   \
        link_write_u64(bytes, program_header + 16, (address));                                                                                                 \
        link_write_u64(bytes, program_header + 24, (address));                                                                                                 \
        link_write_u64(bytes, program_header + 32, (file_bytes));                                                                                              \
        link_write_u64(bytes, program_header + 40, (memory_bytes));                                                                                            \
        link_write_u64(bytes, program_header + 48, (alignment));                                                                                               \
        program_header += ELF_PROGRAM_HEADER_SIZE;                                                                                                             \
    } while (0)
    BUSTER_LINK_PROGRAM_HEADER(6, 4, ELF_HEADER_SIZE, image_base + ELF_HEADER_SIZE, (u64)program_header_count * ELF_PROGRAM_HEADER_SIZE,
                               (u64)program_header_count * ELF_PROGRAM_HEADER_SIZE, 8);
    BUSTER_LINK_PROGRAM_HEADER(3, 4, interpreter_offset, image_base + interpreter_offset, interpreter_size, interpreter_size, 1);
    BUSTER_LINK_PROGRAM_HEADER(1, 5, 0, image_base, read_only_end, read_only_end, ELF_PAGE_SIZE);
    BUSTER_LINK_PROGRAM_HEADER(1, 6, section_offsets[OBJECT_SECTION_DATA], image_base + section_offsets[OBJECT_SECTION_DATA],
                               file_size - section_offsets[OBJECT_SECTION_DATA], writable_memory_end - section_offsets[OBJECT_SECTION_DATA], ELF_PAGE_SIZE);
    BUSTER_LINK_PROGRAM_HEADER(2, 6, dynamic_offset, dynamic_address, dynamic_size, dynamic_size, 8);
    u64 thread_local_file_size = object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length;
    u64 thread_local_memory_size = align_forward(thread_local_file_size, object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment) +
                                   object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size;
    link_write_u32(bytes, program_header, 7);
    link_write_u32(bytes, program_header + 4, 4);
    link_write_u64(bytes, program_header + 8, section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA]);
    link_write_u64(bytes, program_header + 16, image_base + section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA]);
    link_write_u64(bytes, program_header + 24, image_base + section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA]);
    link_write_u64(bytes, program_header + 32, thread_local_file_size);
    link_write_u64(bytes, program_header + 40, thread_local_memory_size);
    link_write_u64(bytes, program_header + 48,
                   BUSTER_MAX(object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].alignment, object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment));
    program_header += ELF_PROGRAM_HEADER_SIZE;
    if (eh_frame_header_size)
    {
        BUSTER_LINK_PROGRAM_HEADER(0x6474e550, 4, eh_frame_header_offset, image_base + eh_frame_header_offset, eh_frame_header_size, eh_frame_header_size, 4);
    }
    BUSTER_LINK_PROGRAM_HEADER(0x6474e551, 6, 0, 0, 0, 0, 16);
#undef BUSTER_LINK_PROGRAM_HEADER
    link_elf_section_table_append(arena, &result, object, image_base, section_offsets,
                                  (LinkElfSectionTableLayout){
                                      .eh_frame_header_offset = eh_frame_header_offset,
                                      .eh_frame_header_size = eh_frame_header_size,
                                      .interpreter_offset = interpreter_offset,
                                      .interpreter_size = interpreter_size,
                                      .plt_offset = plt_offset,
                                      .plt_size = plt_size,
                                      .dynamic_string_offset = dynamic_string_offset,
                                      .dynamic_string_size = dynamic_string_size,
                                      .dynamic_symbol_offset = dynamic_symbol_offset,
                                      .dynamic_symbol_size = dynamic_symbol_size,
                                      .hash_offset = hash_offset,
                                      .hash_size = hash_size,
                                      .relocation_offset = relocation_offset,
                                      .relocation_size = relocation_size,
                                      .got_offset = got_offset,
                                      .got_size = got_size,
                                      .dynamic_offset = dynamic_offset,
                                      .dynamic_size = dynamic_size,
                                      .dynamic = true,
                                  });
    if (options.output_path.length && !link_write_executable_file(options.output_path, result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void link_pe_section_header(u8* bytes, u64 offset, char const* name, u32 virtual_size, u32 virtual_address, u32 raw_size, u32 raw_offset,
                                                u32 characteristics)
{
    memcpy(bytes + offset, name, 8);
    link_write_u32(bytes, offset + 8, virtual_size);
    link_write_u32(bytes, offset + 12, virtual_address);
    link_write_u32(bytes, offset + 16, raw_size);
    link_write_u32(bytes, offset + 20, raw_offset);
    link_write_u32(bytes, offset + 36, characteristics);
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult link_native_executable_elf64_aarch64(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_PAGE_SIZE = 4096,
        ELF_MACHINE_AARCH64 = 183,
    };
    static u32 const entry_stub[] = {
        0xf94003e0, 0x910023e1, 0x8b000c22, 0x91002042, 0x94000000, 0xd2800ba8, 0xd4000001, 0xd4200000,
    };
    if ((options.dynamic_library_count && !options.dynamic_libraries) || object->section_count < OBJECT_SECTION_COUNT || !object->sections ||
        (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        if (object->symbols[symbol_index].section == OBJECT_SECTION_UNDEFINED)
        {
            result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = object->symbols[symbol_index].name;
            return result;
        }
    }
    String8 entry_name = options.entry_symbol.length ? options.entry_symbol : S8("main");
    u32 entry_symbol_index = link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX || object->symbols[entry_symbol_index].section == OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind != OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    LinkElfEhFrameTable eh_frame_table = link_elf_eh_frame_table_build(arena, object);
    if (!eh_frame_table.valid)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    u64 eh_frame_header_size = eh_frame_table.count ? 12 + (u64)eh_frame_table.count * 8 : 0;
    bool has_writable = object->sections[OBJECT_SECTION_DATA].data.length || object->sections[OBJECT_SECTION_ZERO].virtual_size;
    u32 load_program_header_count = has_writable ? 2 : 1;
    u32 program_header_count = load_program_header_count + (eh_frame_header_size != 0);
    u64 header_end = ELF_HEADER_SIZE + (u64)program_header_count * ELF_PROGRAM_HEADER_SIZE;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 entry_stub_offset = align_forward(header_end, 16);
    section_offsets[OBJECT_SECTION_TEXT] = align_forward(entry_stub_offset + sizeof(entry_stub), object->sections[OBJECT_SECTION_TEXT].alignment);
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] = align_forward(section_offsets[OBJECT_SECTION_TEXT] + object->sections[OBJECT_SECTION_TEXT].data.length,
                                                                   object->sections[OBJECT_SECTION_READ_ONLY_DATA].alignment);
    u64 eh_frame_header_offset = align_forward(section_offsets[OBJECT_SECTION_READ_ONLY_DATA] + object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length, 4);
    section_offsets[OBJECT_SECTION_UNWIND] = align_forward(eh_frame_header_offset + eh_frame_header_size, object->sections[OBJECT_SECTION_UNWIND].alignment);
    u64 read_only_end = section_offsets[OBJECT_SECTION_UNWIND] + object->sections[OBJECT_SECTION_UNWIND].data.length;
    section_offsets[OBJECT_SECTION_DATA] = align_forward(read_only_end, ELF_PAGE_SIZE);
    u64 file_size = object->sections[OBJECT_SECTION_DATA].data.length ? section_offsets[OBJECT_SECTION_DATA] + object->sections[OBJECT_SECTION_DATA].data.length
                                                                      : read_only_end;
    section_offsets[OBJECT_SECTION_ZERO] = align_forward(BUSTER_MAX(file_size, section_offsets[OBJECT_SECTION_DATA]), object->sections[OBJECT_SECTION_ZERO].alignment);
    u64 writable_memory_end = section_offsets[OBJECT_SECTION_ZERO] + object->sections[OBJECT_SECTION_ZERO].virtual_size;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(arena, u8, file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(bytes + entry_stub_offset, entry_stub, sizeof(entry_stub));
    for (u32 section = 0; section < OBJECT_SECTION_COUNT; section += 1)
    {
        if (object_section_kind_is_debug((ObjectSectionKind)section))
        {
            continue;
        }
        ByteSlice data = object->sections[section].data;
        if (data.length)
        {
            memcpy(bytes + section_offsets[section], data.pointer, data.length);
        }
    }
    u64 image_base = 0x400000;
    ObjectSymbol* entry_symbol = &object->symbols[entry_symbol_index];
    u64 entry_address = image_base + section_offsets[entry_symbol->section] + entry_symbol->value;
    u64 call_offset = entry_stub_offset + 4 * sizeof(u32);
    s64 call_displacement = (s64)entry_address - (s64)(image_base + call_offset);
    s64 call_words = call_displacement / 4;
    if (call_displacement % 4 || call_words < -(1 << 25) || call_words >= (1 << 25))
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(bytes, call_offset, 0x94000000 | ((u32)call_words & 0x03ffffff));
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[index];
        if (relocation->section < OBJECT_SECTION_COUNT && object_section_kind_is_debug((ObjectSectionKind)relocation->section))
        {
            continue;
        }
        if (relocation->section >= OBJECT_SECTION_COUNT || relocation->symbol >= object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section = &object->sections[relocation->section];
        u64 width = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (relocation->offset > section->data.length || width > section->data.length - relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        if (symbol->section >= OBJECT_SECTION_COUNT)
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 symbol_address = image_base + section_offsets[symbol->section] + symbol->value;
        u64 place_address = image_base + section_offsets[relocation->section] + relocation->offset;
        u64 output_offset = section_offsets[relocation->section] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26)
        {
            s64 displacement = (s64)symbol_address + relocation->addend - (s64)place_address;
            s64 words = displacement / 4;
            if (displacement % 4 || words < -(1 << 25) || words >= (1 << 25))
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, 0x94000000 | ((u32)words & 0x03ffffff));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_PREL32)
        {
            s64 value = (s64)symbol_address + relocation->addend - (s64)place_address;
            if (value < INT32_MIN || value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)(s32)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            link_write_u64(bytes, output_offset, symbol_address + (u64)relocation->addend);
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    if (!link_elf_eh_frame_header_write(bytes, file_size, &eh_frame_table, object, image_base, section_offsets, eh_frame_header_offset))
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    link_write_u16(bytes, 16, 2);
    link_write_u16(bytes, 18, ELF_MACHINE_AARCH64);
    link_write_u32(bytes, 20, 1);
    link_write_u64(bytes, 24, image_base + entry_stub_offset);
    link_write_u64(bytes, 32, ELF_HEADER_SIZE);
    link_write_u16(bytes, 52, ELF_HEADER_SIZE);
    link_write_u16(bytes, 54, ELF_PROGRAM_HEADER_SIZE);
    link_write_u16(bytes, 56, (u16)program_header_count);
    u64 program_header = ELF_HEADER_SIZE;
    link_write_u32(bytes, program_header, 1);
    link_write_u32(bytes, program_header + 4, 5);
    link_write_u64(bytes, program_header + 16, image_base);
    link_write_u64(bytes, program_header + 24, image_base);
    link_write_u64(bytes, program_header + 32, read_only_end);
    link_write_u64(bytes, program_header + 40, read_only_end);
    link_write_u64(bytes, program_header + 48, ELF_PAGE_SIZE);
    if (load_program_header_count == 2)
    {
        program_header += ELF_PROGRAM_HEADER_SIZE;
        u64 writable_file_size = file_size > section_offsets[OBJECT_SECTION_DATA] ? file_size - section_offsets[OBJECT_SECTION_DATA] : 0;
        u64 writable_memory_size = writable_memory_end - section_offsets[OBJECT_SECTION_DATA];
        link_write_u32(bytes, program_header, 1);
        link_write_u32(bytes, program_header + 4, 6);
        link_write_u64(bytes, program_header + 8, section_offsets[OBJECT_SECTION_DATA]);
        link_write_u64(bytes, program_header + 16, image_base + section_offsets[OBJECT_SECTION_DATA]);
        link_write_u64(bytes, program_header + 24, image_base + section_offsets[OBJECT_SECTION_DATA]);
        link_write_u64(bytes, program_header + 32, writable_file_size);
        link_write_u64(bytes, program_header + 40, writable_memory_size);
        link_write_u64(bytes, program_header + 48, ELF_PAGE_SIZE);
    }
    if (eh_frame_header_size)
    {
        program_header = ELF_HEADER_SIZE + (u64)load_program_header_count * ELF_PROGRAM_HEADER_SIZE;
        link_write_u32(bytes, program_header, 0x6474e550);
        link_write_u32(bytes, program_header + 4, 4);
        link_write_u64(bytes, program_header + 8, eh_frame_header_offset);
        link_write_u64(bytes, program_header + 16, image_base + eh_frame_header_offset);
        link_write_u64(bytes, program_header + 24, image_base + eh_frame_header_offset);
        link_write_u64(bytes, program_header + 32, eh_frame_header_size);
        link_write_u64(bytes, program_header + 40, eh_frame_header_size);
        link_write_u64(bytes, program_header + 48, 4);
    }
    link_elf_section_table_append(arena, &result, object, image_base, section_offsets,
                                  (LinkElfSectionTableLayout){
                                      .eh_frame_header_offset = eh_frame_header_offset,
                                      .eh_frame_header_size = eh_frame_header_size,
                                  });
    if (options.output_path.length && !link_write_executable_file(options.output_path, result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 link_aarch64_adrp(u32 destination, u64 instruction_address, u64 target_address, bool* valid)
{
    s64 instruction_page = (s64)(instruction_address & ~UINT64_C(0xfff));
    s64 target_page = (s64)(target_address & ~UINT64_C(0xfff));
    s64 pages = (target_page - instruction_page) / 4096;
    if (destination > 31 || pages < -(1 << 20) || pages >= (1 << 20))
    {
        *valid = false;
        return 0;
    }
    u32 immediate = (u32)pages & 0x1fffff;
    return 0x90000000 | ((immediate & 3) << 29) | (((immediate >> 2) & 0x7ffff) << 5) | destination;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult link_native_executable_elf64_aarch64_dynamic(Arena* arena, ObjectFile* object,
                                                                                            NativeExecutableLinkOptions options)
{
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_DYNAMIC_SIZE = 16,
        ELF_DYNAMIC_COUNT = 12,
        ELF_RELOCATION_SIZE = 24,
        ELF_PLT_ENTRY_SIZE = 16,
        ELF_GOT_RESERVED_COUNT = 3,
    };
    static u32 const entry_stub[] = {
        0xf94003e0, 0x910023e1, 0x8b000c22, 0x91002042, 0x94000000, 0xd2800ba8, 0xd4000001, 0xd4200000,
    };
    static char8 const interpreter[] = "/lib/ld-linux-aarch64.so.1";
    NativeExecutableLinkResult result = {0};
    ObjectRelocation* converted_relocations = arena_allocate(arena, ObjectRelocation, object->relocation_count);
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        converted_relocations[index] = object->relocations[index];
        if (converted_relocations[index].kind == OBJECT_RELOCATION_AARCH64_CALL26)
        {
            converted_relocations[index].kind = OBJECT_RELOCATION_X86_64_PC32;
        }
        else if (converted_relocations[index].kind == OBJECT_RELOCATION_AARCH64_PREL32)
        {
            converted_relocations[index].kind = OBJECT_RELOCATION_X86_64_PC32;
        }
        else if (converted_relocations[index].kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 ||
                 converted_relocations[index].kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12)
        {
            converted_relocations[index].kind = OBJECT_RELOCATION_X86_64_TPOFF32;
        }
    }
    ObjectFile converted = *object;
    converted.relocations = converted_relocations;
    NativeExecutableLinkOptions staging_options = options;
    staging_options.output_path = (String8){0};
    result = link_native_executable_elf64_x86_64_dynamic(arena, &converted, staging_options);
    if (result.error != LINK_ERROR_NONE)
    {
        return result;
    }
    u8* bytes = result.executable.pointer;
    u64 image_base = 0x400000;
    u32 program_header_count = bytes[56] | ((u32)bytes[57] << 8);
    u64 header_end = ELF_HEADER_SIZE + (u64)program_header_count * ELF_PROGRAM_HEADER_SIZE;
    u64 entry_stub_offset = align_forward(header_end, 16);
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    section_offsets[OBJECT_SECTION_TEXT] = align_forward(entry_stub_offset + 35, object->sections[OBJECT_SECTION_TEXT].alignment);
    u64 plt_offset = align_forward(section_offsets[OBJECT_SECTION_TEXT] + object->sections[OBJECT_SECTION_TEXT].data.length, 16);
    u32 import_count = 0;
    u32* import_indices = arena_allocate(arena, u32, object->symbol_count);
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        if (object->symbols[symbol_index].section == OBJECT_SECTION_UNDEFINED)
        {
            import_indices[symbol_index] = import_count++;
        }
    }
    u64 plt_size = (u64)(import_count + 1) * ELF_PLT_ENTRY_SIZE;
    memset(bytes + plt_offset, 0, plt_size);
    memcpy(bytes + entry_stub_offset, entry_stub, sizeof(entry_stub));
    memcpy(bytes + link_read_u64(bytes, ELF_HEADER_SIZE + ELF_PROGRAM_HEADER_SIZE + 8), interpreter, sizeof(interpreter));
    link_write_u16(bytes, 18, 183);
    u64 dynamic_program_header = ELF_HEADER_SIZE + 4 * ELF_PROGRAM_HEADER_SIZE;
    u64 dynamic_offset = link_read_u64(bytes, dynamic_program_header + 8);
    u64 relocation_offset = 0;
    u32 dynamic_count = ELF_DYNAMIC_COUNT + options.dynamic_library_count;
    for (u32 index = 0; index < dynamic_count; index += 1)
    {
        u64 entry = dynamic_offset + (u64)index * ELF_DYNAMIC_SIZE;
        u64 tag = link_read_u64(bytes, entry);
        if (tag == 3)
        {
            link_write_u64(bytes, entry, 24);
            link_write_u64(bytes, entry + 8, 0);
        }
        else if (tag == 23)
        {
            u64 address = link_read_u64(bytes, entry + 8);
            relocation_offset = address - image_base;
        }
    }
    if (!relocation_offset)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    bool valid = true;
    for (u32 import_index = 0; import_index < import_count; import_index += 1)
    {
        u64 relocation = relocation_offset + (u64)import_index * ELF_RELOCATION_SIZE;
        u64 slot_address = link_read_u64(bytes, relocation);
        link_write_u64(bytes, relocation + 8, ((u64)(import_index + 1) << 32) | 1026);
        u64 thunk_offset = plt_offset + (u64)(import_index + 1) * ELF_PLT_ENTRY_SIZE;
        u64 thunk_address = image_base + thunk_offset;
        u64 page_offset = slot_address & 0xfff;
        if (page_offset % 8 || page_offset / 8 > 0xfff)
        {
            valid = false;
            break;
        }
        link_write_u32(bytes, thunk_offset, link_aarch64_adrp(16, thunk_address, slot_address, &valid));
        link_write_u32(bytes, thunk_offset + 4, 0xf9400000 | ((u32)(page_offset / 8) << 10) | (16 << 5) | 17);
        link_write_u32(bytes, thunk_offset + 8, 0xd61f0220);
        link_write_u32(bytes, thunk_offset + 12, 0xd503201f);
        link_write_u64(bytes, (slot_address - image_base), 0);
    }
    String8 entry_name = options.entry_symbol.length ? options.entry_symbol : S8("main");
    u32 entry_symbol_index = link_symbol_find(object, entry_name);
    if (!valid || entry_symbol_index == UINT32_MAX)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    ObjectSymbol* entry_symbol = &object->symbols[entry_symbol_index];
    u64 entry_address = image_base + section_offsets[entry_symbol->section] + entry_symbol->value;
    u64 call_offset = entry_stub_offset + 4 * sizeof(u32);
    s64 call_displacement = (s64)entry_address - (s64)(image_base + call_offset);
    s64 call_words = call_displacement / 4;
    if (call_displacement % 4 || call_words < -(1 << 25) || call_words >= (1 << 25))
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(bytes, call_offset, 0x94000000 | ((u32)call_words & 0x03ffffff));
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[index];
        if (relocation->kind != OBJECT_RELOCATION_AARCH64_CALL26 && relocation->kind != OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 &&
            relocation->kind != OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12)
        {
            continue;
        }
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        u64 output_offset = section_offsets[relocation->section] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 || relocation->kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12)
        {
            if ((symbol->section != OBJECT_SECTION_THREAD_LOCAL_DATA && symbol->section != OBJECT_SECTION_THREAD_LOCAL_ZERO) || relocation->addend < 0)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            u64 initialized_size =
                align_forward(object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length, object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment);
            u64 symbol_offset = symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO ? initialized_size + symbol->value : symbol->value;
            u64 thread_pointer_offset = 16 + symbol_offset + (u64)relocation->addend;
            if (thread_pointer_offset > 0xffffff)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            u32 instruction = link_read_u32(object->sections[relocation->section].data.pointer, relocation->offset);
            u32 immediate =
                relocation->kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 ? (u32)(thread_pointer_offset >> 12) : (u32)(thread_pointer_offset & 0xfff);
            instruction &= ~(UINT32_C(0xfff) << 10);
            instruction |= immediate << 10;
            link_write_u32(bytes, output_offset, instruction);
            continue;
        }
        u64 symbol_address = 0;
        if (symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index = import_indices[relocation->symbol];
            if (import_index == UINT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_address = image_base + plt_offset + (u64)(import_index + 1) * ELF_PLT_ENTRY_SIZE;
        }
        else
        {
            symbol_address = image_base + section_offsets[symbol->section] + symbol->value;
        }
        u64 place_address = image_base + output_offset;
        s64 displacement = (s64)symbol_address + relocation->addend - (s64)place_address;
        s64 words = displacement / 4;
        if (displacement % 4 || words < -(1 << 25) || words >= (1 << 25))
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(bytes, output_offset, 0x94000000 | ((u32)words & 0x03ffffff));
    }
    if (options.output_path.length && !link_write_executable_file(options.output_path, result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

enum
{
    LINK_PE_STACK_RESERVE = 8 * 1024 * 1024,
};

BUSTER_GLOBAL_LOCAL String8 link_pe_pdb_path(Arena* arena, String8 executable_path)
{
    if (!executable_path.length)
    {
        executable_path = S8("a.exe");
    }
    u64 dot = executable_path.length;
    u64 slash = 0;
    for (u64 index = 0; index < executable_path.length; index += 1)
    {
        if (executable_path.pointer[index] == '/' || executable_path.pointer[index] == '\\')
        {
            slash = index + 1;
        }
        else if (executable_path.pointer[index] == '.' && index >= slash)
        {
            dot = index;
        }
    }
    return string_format_z(arena, S8("{S8}.pdb"), string_slice(executable_path, 0, dot));
}

BUSTER_TEST_F_DECL ByteSlice link_pe_resolved_codeview(Arena* arena, ObjectFile* object, ObjectDebugModule* debug_module,
                                                       u32 const* object_output_sections, u64 const* object_section_offsets,
                                                       u32 output_section_count)
{
    ByteSlice result = {0};
    if (!arena || !object || !debug_module || !object_output_sections || !object_section_offsets || !output_section_count ||
        debug_module->symbols_size > UINT32_MAX ||
        debug_module->symbols_offset > object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.length ||
        debug_module->symbols_size > object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.length - debug_module->symbols_offset)
    {
        return result;
    }
    u8* bytes = arena_allocate(arena, u8, debug_module->symbols_size);
    memcpy(bytes, object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.pointer + debug_module->symbols_offset, debug_module->symbols_size);
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation* relocation = object->relocations + relocation_index;
        if (relocation->section != OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS || relocation->offset < debug_module->symbols_offset ||
            relocation->offset - debug_module->symbols_offset >= debug_module->symbols_size || relocation->symbol >= object->symbol_count)
        {
            continue;
        }
        u64 offset = relocation->offset - debug_module->symbols_offset;
        ObjectSymbol* symbol = object->symbols + relocation->symbol;
        if (symbol->section >= OBJECT_SECTION_COUNT || object_output_sections[symbol->section] >= output_section_count ||
            object_output_sections[symbol->section] >= UINT16_MAX)
        {
            return (ByteSlice){0};
        }
        if (relocation->kind == OBJECT_RELOCATION_COFF_SECREL32)
        {
            if (offset + 4 > debug_module->symbols_size || symbol->value > INT64_MAX || object_section_offsets[symbol->section] > INT64_MAX)
            {
                return (ByteSlice){0};
            }
            s64 signed_value = (s64)symbol->value;
            if ((relocation->addend > 0 && signed_value > INT64_MAX - relocation->addend) ||
                (relocation->addend < 0 && signed_value < INT64_MIN - relocation->addend))
            {
                return (ByteSlice){0};
            }
            signed_value += relocation->addend;
            if (signed_value > INT64_MAX - (s64)object_section_offsets[symbol->section])
            {
                return (ByteSlice){0};
            }
            signed_value += (s64)object_section_offsets[symbol->section];
            if (signed_value < 0 || signed_value > UINT32_MAX)
            {
                return (ByteSlice){0};
            }
            memcpy(bytes + offset, &(u32){(u32)signed_value}, sizeof(u32));
        }
        else if (relocation->kind == OBJECT_RELOCATION_COFF_SECTION16)
        {
            if (offset + 2 > debug_module->symbols_size)
            {
                return (ByteSlice){0};
            }
            u16 section = (u16)(object_output_sections[symbol->section] + 1);
            memcpy(bytes + offset, &section, sizeof(section));
        }
    }
    result = (ByteSlice){.pointer = bytes, .length = debug_module->symbols_size};
    return result;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult link_native_executable_pe64(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        PE_OFFSET = 0x80,
        PE_COFF_HEADER_SIZE = 20,
        PE_OPTIONAL_HEADER_SIZE = 240,
        PE_SECTION_HEADER_SIZE = 40,
        PE_SECTION_COUNT = 6 + 1,
        PE_FILE_ALIGNMENT = 0x200,
        PE_SECTION_ALIGNMENT = 0x1000,
        PE_IMPORT_DESCRIPTOR_SIZE = 20,
        PE_IMAGE_BASE_LOW = 0x40000000,
        PE_IMAGE_BASE_HIGH = 1,
    };
    static u8 const entry_stub_x86_64[] = {
        0x48, 0x83, 0xec, 0x38, 0x31, 0xc9, 0xff, 0xc1, 0xff, 0x15, 0,    0,    0, 0, 0xff, 0x15, 0,    0,
        0,    0,    0x8b, 0x00, 0x89, 0x44, 0x24, 0x20, 0xff, 0x15, 0,    0,    0, 0, 0x48, 0x8b, 0x10, 0x8b,
        0x4c, 0x24, 0x20, 0xe8, 0,    0,    0,    0,    0x89, 0xc1, 0xff, 0x15, 0, 0, 0,    0,    0xcc,
    };
    static u32 const entry_stub_aarch64[] = {
        0xa9bf7bfd, 0x910003fd, 0x94000000, 0x94000000, 0xd4200000,
    };
    static char8 const runtime_library[] = "ucrtbase.dll";
    static char8 const kernel_library[] = "kernel32.dll";
    static char8 const exit_name[] = "ExitProcess";
    static String8 const startup_names[] = {
        S8_INITIALIZER("_configure_narrow_argv"),
        S8_INITIALIZER("__p___argc"),
        S8_INITIALIZER("__p___argv"),
    };
    bool aarch64 = object->target.cpu_arch == CPU_ARCH_AARCH64;
    if ((options.dynamic_library_count && !options.dynamic_libraries) || (options.runtime_exported_symbol_count && !options.runtime_exported_symbols) ||
        object->section_count < OBJECT_SECTION_COUNT || !object->sections || (object->symbol_count && !object->symbols) ||
        (object->relocation_count && !object->relocations) || (object->target.cpu_arch != CPU_ARCH_X86_64 && !aarch64))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u32 import_count = 0;
    u64 imported_name_size = 0;
    u32* import_indices = arena_allocate(arena, u32, object->symbol_count);
    u32* import_slots = arena_allocate(arena, u32, object->symbol_count);
    u32* import_groups = arena_allocate(arena, u32, object->symbol_count);
    u32 library_group_count = options.dynamic_library_count + 1;
    u32* group_import_counts = arena_allocate(arena, u32, library_group_count);
    memset(group_import_counts, 0, sizeof(*group_import_counts) * library_group_count);
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        import_slots[symbol_index] = UINT32_MAX;
        import_groups[symbol_index] = UINT32_MAX;
        ObjectSymbol* symbol = &object->symbols[symbol_index];
        if (symbol->section != OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        if (!symbol->global || (symbol->kind != OBJECT_SYMBOL_FUNCTION && symbol->kind != OBJECT_SYMBOL_DATA) || !symbol->name.length ||
            symbol->name.length > UINT32_MAX ||
            imported_name_size > UINT32_MAX - symbol->name.length - 3)
        {
            result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = symbol->name;
            return result;
        }
        u32 group = 0;
        bool library_found = false;
        for (u32 library_index = 0; library_index < options.dynamic_library_count; library_index += 1)
        {
            NativeDynamicLibrary* library = &options.dynamic_libraries[library_index];
            for (u32 export_index = 0; export_index < library->exported_symbol_count; export_index += 1)
            {
                if (string_equal(symbol->name, library->exported_symbols[export_index]))
                {
                    group = library_index + 1;
                    library_found = true;
                    break;
                }
            }
            if (group)
            {
                break;
            }
        }
        if (!library_found && options.dynamic_library_count)
        {
            bool runtime_found = false;
            for (u32 export_index = 0; export_index < options.runtime_exported_symbol_count; export_index += 1)
            {
                if (string_equal(symbol->name, options.runtime_exported_symbols[export_index]))
                {
                    runtime_found = true;
                    break;
                }
            }
            if (!options.runtime_exports_known || !runtime_found)
            {
                result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
                result.symbol = symbol->name;
                return result;
            }
        }
        import_indices[symbol_index] = import_count;
        import_groups[symbol_index] = group;
        group_import_counts[group] += 1;
        imported_name_size += align_forward(symbol->name.length + 3, 2);
        import_count += 1;
    }
    if (!aarch64)
    {
        group_import_counts[0] += (u32)BUSTER_ARRAY_LENGTH(startup_names);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(startup_names); index += 1)
        {
            imported_name_size += align_forward(startup_names[index].length + 3, 2);
        }
    }
    u32* group_slot_offsets = arena_allocate(arena, u32, library_group_count);
    u32* group_slot_counts = arena_allocate(arena, u32, library_group_count);
    u32 total_import_slots = 0;
    for (u32 group = 0; group < library_group_count; group += 1)
    {
        group_slot_offsets[group] = total_import_slots;
        group_slot_counts[group] = 0;
        if (group_import_counts[group])
        {
            total_import_slots += group_import_counts[group] + 1;
        }
    }
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        u32 group = import_groups[symbol_index];
        if (group == UINT32_MAX)
        {
            continue;
        }
        import_slots[symbol_index] = group_slot_offsets[group] + group_slot_counts[group]++;
    }
    u32* startup_import_slots = arena_allocate(arena, u32, BUSTER_ARRAY_LENGTH(startup_names));
    memset(startup_import_slots, 0, sizeof(*startup_import_slots) * BUSTER_ARRAY_LENGTH(startup_names));
    if (!aarch64)
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(startup_names); index += 1)
        {
            startup_import_slots[index] = group_slot_offsets[0] + group_slot_counts[0]++;
        }
    }
    String8 entry_name = options.entry_symbol.length ? options.entry_symbol : S8("main");
    u32 entry_symbol_index = link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX || object->symbols[entry_symbol_index].section == OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind != OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    bool emit_debug = options.debug_info && object->debug_module_count && object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.length;
    u16 pe_section_count = emit_debug ? PE_SECTION_COUNT : PE_SECTION_COUNT - 1;
    String8 pdb_path = emit_debug ? link_pe_pdb_path(arena, options.output_path) : (String8){0};
    u64 debug_virtual_size = emit_debug ? 28 + 24 + pdb_path.length + 1 : 0;
    u64 debug_raw_size = align_forward(BUSTER_MAX(debug_virtual_size, 1), PE_FILE_ALIGNMENT);
    u64 header_size =
        align_forward(PE_OFFSET + 4 + PE_COFF_HEADER_SIZE + PE_OPTIONAL_HEADER_SIZE + (u64)pe_section_count * PE_SECTION_HEADER_SIZE, PE_FILE_ALIGNMENT);
    u32* section_rvas = arena_allocate(arena, u32, PE_SECTION_COUNT);
    u32* section_raw_offsets = arena_allocate(arena, u32, PE_SECTION_COUNT);
    u32* object_output_sections = arena_allocate(arena, u32, OBJECT_SECTION_COUNT);
    u64* object_section_offsets = arena_allocate(arena, u64, OBJECT_SECTION_COUNT);
    memset(section_rvas, 0, sizeof(*section_rvas) * PE_SECTION_COUNT);
    memset(section_raw_offsets, 0, sizeof(*section_raw_offsets) * PE_SECTION_COUNT);
    memset(object_output_sections, 0xff, sizeof(*object_output_sections) * OBJECT_SECTION_COUNT);
    memset(object_section_offsets, 0, sizeof(*object_section_offsets) * OBJECT_SECTION_COUNT);
    section_rvas[0] = PE_SECTION_ALIGNMENT;
    section_raw_offsets[0] = (u32)header_size;
    object_output_sections[OBJECT_SECTION_TEXT] = 0;
    object_output_sections[OBJECT_SECTION_READ_ONLY_DATA] = 1;
    object_output_sections[OBJECT_SECTION_DATA] = 2;
    object_output_sections[OBJECT_SECTION_ZERO] = 3;
    object_output_sections[OBJECT_SECTION_THREAD_LOCAL_DATA] = 4;
    object_output_sections[OBJECT_SECTION_THREAD_LOCAL_ZERO] = 4;
    u64 entry_stub_offset = 0;
    u64 entry_stub_size = aarch64 ? sizeof(entry_stub_aarch64) : sizeof(entry_stub_x86_64);
    object_section_offsets[OBJECT_SECTION_TEXT] = align_forward(entry_stub_size, object->sections[OBJECT_SECTION_TEXT].alignment);
    u64 thunk_offset = align_forward(object_section_offsets[OBJECT_SECTION_TEXT] + object->sections[OBJECT_SECTION_TEXT].data.length, 16);
    u32 thunk_entry_size = aarch64 ? 16 : 6;
    u64 thunk_size = (u64)(import_count + (aarch64 ? 1 : 0)) * thunk_entry_size;
    u64 text_virtual_size = thunk_offset + thunk_size;
    u64 text_raw_size = align_forward(text_virtual_size, PE_FILE_ALIGNMENT);
    section_rvas[1] = (u32)align_forward(section_rvas[0] + text_virtual_size, PE_SECTION_ALIGNMENT);
    section_raw_offsets[1] = section_raw_offsets[0] + (u32)text_raw_size;
    object_section_offsets[OBJECT_SECTION_READ_ONLY_DATA] = 0;
    u64 read_only_virtual_size = object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length;
    u64 read_only_raw_size = align_forward(BUSTER_MAX(read_only_virtual_size, 1), PE_FILE_ALIGNMENT);
    section_rvas[2] = (u32)align_forward(section_rvas[1] + BUSTER_MAX(read_only_virtual_size, 1), PE_SECTION_ALIGNMENT);
    section_raw_offsets[2] = section_raw_offsets[1] + (u32)read_only_raw_size;
    object_section_offsets[OBJECT_SECTION_DATA] = 0;
    u64 data_virtual_size = object->sections[OBJECT_SECTION_DATA].data.length;
    u64 data_raw_size = align_forward(BUSTER_MAX(data_virtual_size, 1), PE_FILE_ALIGNMENT);
    section_rvas[3] = (u32)align_forward(section_rvas[2] + BUSTER_MAX(data_virtual_size, 1), PE_SECTION_ALIGNMENT);
    object_section_offsets[OBJECT_SECTION_ZERO] = 0;
    u64 zero_virtual_size = object->sections[OBJECT_SECTION_ZERO].virtual_size;
    section_rvas[4] = (u32)align_forward(section_rvas[3] + BUSTER_MAX(zero_virtual_size, 1), PE_SECTION_ALIGNMENT);
    section_raw_offsets[4] = section_raw_offsets[2] + (u32)data_raw_size;
    object_section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA] = 0;
    object_section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO] =
        align_forward(object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length, object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment);
    u64 tls_initialized_size = object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length;
    u64 tls_virtual_size = object_section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO] + object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size;
    u64 tls_raw_size = align_forward(BUSTER_MAX(tls_initialized_size, 1), PE_FILE_ALIGNMENT);
    section_rvas[5] = (u32)align_forward(section_rvas[4] + BUSTER_MAX(tls_virtual_size, 1), PE_SECTION_ALIGNMENT);
    section_raw_offsets[5] = section_raw_offsets[4] + (u32)tls_raw_size;
    u32 active_group_count = 0;
    for (u32 group = 0; group < library_group_count; group += 1)
    {
        active_group_count += group_import_counts[group] != 0;
    }
    u32 import_descriptor_count = active_group_count + 2;
    u64 tls_directory_offset = 0;
    u64 tls_index_offset = 40;
    u64 import_descriptor_offset = align_forward(tls_index_offset + 4, 8);
    u64 runtime_lookup_offset = align_forward(import_descriptor_offset + (u64)import_descriptor_count * PE_IMPORT_DESCRIPTOR_SIZE, 8);
    u64 runtime_address_offset = runtime_lookup_offset + (u64)total_import_slots * sizeof(u64);
    u64 kernel_lookup_offset = runtime_address_offset + (u64)total_import_slots * sizeof(u64);
    u64 kernel_address_offset = kernel_lookup_offset + 2 * sizeof(u64);
    u64 import_name_offset = kernel_address_offset + 2 * sizeof(u64);
    u64* library_name_offsets = arena_allocate(arena, u64, library_group_count);
    u64 library_name_cursor = import_name_offset + imported_name_size;
    library_name_offsets[0] = library_name_cursor;
    library_name_cursor += sizeof(runtime_library);
    for (u32 library_index = 0; library_index < options.dynamic_library_count; library_index += 1)
    {
        String8 library = options.dynamic_libraries[library_index].name;
        if (!library.length || library.length >= UINT32_MAX)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        library_name_offsets[library_index + 1] = library_name_cursor;
        library_name_cursor += library.length + 1;
    }
    u64 kernel_library_offset = library_name_cursor;
    u64 exit_import_name_offset = align_forward(kernel_library_offset + sizeof(kernel_library), 2);
    u64 import_virtual_size = exit_import_name_offset + 2 + sizeof(exit_name);
    u64 import_raw_size = align_forward(import_virtual_size, PE_FILE_ALIGNMENT);
    u64 base_file_size = section_raw_offsets[5] + import_raw_size;
    if (emit_debug)
    {
        section_rvas[6] = (u32)align_forward(section_rvas[5] + BUSTER_MAX(import_virtual_size, 1), PE_SECTION_ALIGNMENT);
        section_raw_offsets[6] = (u32)base_file_size;
    }
    u64 file_size = base_file_size + (emit_debug ? debug_raw_size : 0);
    u64 image_size = align_forward((emit_debug ? section_rvas[6] + debug_virtual_size : section_rvas[5] + import_virtual_size), PE_SECTION_ALIGNMENT);
    if (file_size > UINT32_MAX || image_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(arena, u8, file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(bytes + section_raw_offsets[0] + entry_stub_offset, aarch64 ? (void const*)entry_stub_aarch64 : (void const*)entry_stub_x86_64, entry_stub_size);
    for (u32 section = 0; section < OBJECT_SECTION_COUNT; section += 1)
    {
        if (object_section_kind_is_debug((ObjectSectionKind)section))
        {
            continue;
        }
        u32 output_section = object_output_sections[section];
        ByteSlice data = object->sections[section].data;
        if (data.length)
        {
            memcpy(bytes + section_raw_offsets[output_section] + object_section_offsets[section], data.pointer, data.length);
        }
    }
    u32 import_section_rva = section_rvas[5];
    u64 import_section_raw = section_raw_offsets[5];
    u64 pe_image_base = ((u64)PE_IMAGE_BASE_HIGH << 32) | PE_IMAGE_BASE_LOW;
    link_write_u64(bytes, import_section_raw + tls_directory_offset, pe_image_base + section_rvas[4]);
    link_write_u64(bytes, import_section_raw + tls_directory_offset + 8, pe_image_base + section_rvas[4] + tls_initialized_size);
    link_write_u64(bytes, import_section_raw + tls_directory_offset + 16, pe_image_base + import_section_rva + tls_index_offset);
    link_write_u32(bytes, import_section_raw + tls_directory_offset + 32, (u32)(tls_virtual_size - tls_initialized_size));
    u64 imported_name_cursor = import_name_offset;
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        u32 import_index = import_indices[symbol_index];
        if (import_index == UINT32_MAX)
        {
            continue;
        }
        ObjectSymbol* symbol = &object->symbols[symbol_index];
        u32 import_slot = import_slots[symbol_index];
        if (import_slot == UINT32_MAX)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        u32 name_rva = import_section_rva + (u32)imported_name_cursor;
        link_write_u64(bytes, import_section_raw + runtime_lookup_offset + (u64)import_slot * sizeof(u64), name_rva);
        link_write_u64(bytes, import_section_raw + runtime_address_offset + (u64)import_slot * sizeof(u64), name_rva);
        memcpy(bytes + import_section_raw + imported_name_cursor + 2, symbol->name.pointer, symbol->name.length);
        imported_name_cursor += align_forward(symbol->name.length + 3, 2);
        u64 thunk_raw = section_raw_offsets[0] + thunk_offset + (u64)import_index * thunk_entry_size;
        u64 thunk_rva = section_rvas[0] + thunk_offset + (u64)import_index * thunk_entry_size;
        u64 iat_rva = import_section_rva + runtime_address_offset + (u64)import_slot * sizeof(u64);
        if (!aarch64)
        {
            bytes[thunk_raw] = 0xff;
            bytes[thunk_raw + 1] = 0x25;
            link_write_u32(bytes, thunk_raw + 2, (u32)(s32)((s64)iat_rva - (s64)(thunk_rva + 6)));
        }
        else
        {
            u64 slot_address = (((u64)PE_IMAGE_BASE_HIGH << 32) | PE_IMAGE_BASE_LOW) + iat_rva;
            u64 thunk_address = (((u64)PE_IMAGE_BASE_HIGH << 32) | PE_IMAGE_BASE_LOW) + thunk_rva;
            u64 page_offset = slot_address & 0xfff;
            bool adrp_valid = true;
            link_write_u32(bytes, thunk_raw, link_aarch64_adrp(16, thunk_address, slot_address, &adrp_valid));
            if (!adrp_valid || page_offset % 8)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, thunk_raw + 4, 0xf9400000 | ((u32)(page_offset / 8) << 10) | (16 << 5) | 17);
            link_write_u32(bytes, thunk_raw + 8, 0xd61f0220);
            link_write_u32(bytes, thunk_raw + 12, 0xd503201f);
        }
    }
    if (!aarch64)
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(startup_names); index += 1)
        {
            u32 name_rva = import_section_rva + (u32)imported_name_cursor;
            u32 import_slot = startup_import_slots[index];
            link_write_u64(bytes, import_section_raw + runtime_lookup_offset + (u64)import_slot * sizeof(u64), name_rva);
            link_write_u64(bytes, import_section_raw + runtime_address_offset + (u64)import_slot * sizeof(u64), name_rva);
            memcpy(bytes + import_section_raw + imported_name_cursor + 2, startup_names[index].pointer, startup_names[index].length);
            imported_name_cursor += align_forward(startup_names[index].length + 3, 2);
        }
    }
    memcpy(bytes + import_section_raw + library_name_offsets[0], runtime_library, sizeof(runtime_library));
    for (u32 library_index = 0; library_index < options.dynamic_library_count; library_index += 1)
    {
        String8 library = options.dynamic_libraries[library_index].name;
        memcpy(bytes + import_section_raw + library_name_offsets[library_index + 1], library.pointer, library.length);
        bytes[import_section_raw + library_name_offsets[library_index + 1] + library.length] = 0;
    }
    memcpy(bytes + import_section_raw + kernel_library_offset, kernel_library, sizeof(kernel_library));
    memcpy(bytes + import_section_raw + exit_import_name_offset + 2, exit_name, sizeof(exit_name));
    u32 exit_name_rva = import_section_rva + (u32)exit_import_name_offset;
    link_write_u64(bytes, import_section_raw + kernel_lookup_offset, exit_name_rva);
    link_write_u64(bytes, import_section_raw + kernel_address_offset, exit_name_rva);
    u64 descriptor = import_section_raw + import_descriptor_offset;
    for (u32 group = 0; group < library_group_count; group += 1)
    {
        if (!group_import_counts[group])
        {
            continue;
        }
        link_write_u32(bytes, descriptor, import_section_rva + (u32)runtime_lookup_offset + (u32)(group_slot_offsets[group] * sizeof(u64)));
        link_write_u32(bytes, descriptor + 12, import_section_rva + (u32)library_name_offsets[group]);
        link_write_u32(bytes, descriptor + 16, import_section_rva + (u32)runtime_address_offset + (u32)(group_slot_offsets[group] * sizeof(u64)));
        descriptor += PE_IMPORT_DESCRIPTOR_SIZE;
    }
    link_write_u32(bytes, descriptor, import_section_rva + (u32)kernel_lookup_offset);
    link_write_u32(bytes, descriptor + 12, import_section_rva + (u32)kernel_library_offset);
    link_write_u32(bytes, descriptor + 16, import_section_rva + (u32)kernel_address_offset);
    u64 entry_rva = section_rvas[0] + entry_stub_offset;
    ObjectSymbol* entry_symbol = &object->symbols[entry_symbol_index];
    u64 main_rva = section_rvas[object_output_sections[entry_symbol->section]] + object_section_offsets[entry_symbol->section] + entry_symbol->value;
    u64 exit_iat_rva = import_section_rva + kernel_address_offset;
    if (!aarch64)
    {
        u64 configure_argv_iat_rva = import_section_rva + runtime_address_offset + (u64)startup_import_slots[0] * sizeof(u64);
        u64 argc_iat_rva = import_section_rva + runtime_address_offset + (u64)startup_import_slots[1] * sizeof(u64);
        u64 argv_iat_rva = import_section_rva + runtime_address_offset + (u64)startup_import_slots[2] * sizeof(u64);
        s64 configure_argv_displacement = (s64)configure_argv_iat_rva - (s64)(entry_rva + 14);
        s64 argc_displacement = (s64)argc_iat_rva - (s64)(entry_rva + 20);
        s64 argv_displacement = (s64)argv_iat_rva - (s64)(entry_rva + 32);
        s64 main_displacement = (s64)main_rva - (s64)(entry_rva + 44);
        s64 exit_displacement = (s64)exit_iat_rva - (s64)(entry_rva + 52);
        if (configure_argv_displacement < INT32_MIN || configure_argv_displacement > INT32_MAX || argc_displacement < INT32_MIN ||
            argc_displacement > INT32_MAX || argv_displacement < INT32_MIN || argv_displacement > INT32_MAX || main_displacement < INT32_MIN ||
            main_displacement > INT32_MAX || exit_displacement < INT32_MIN || exit_displacement > INT32_MAX)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(bytes, section_raw_offsets[0] + 10, (u32)(s32)configure_argv_displacement);
        link_write_u32(bytes, section_raw_offsets[0] + 16, (u32)(s32)argc_displacement);
        link_write_u32(bytes, section_raw_offsets[0] + 28, (u32)(s32)argv_displacement);
        link_write_u32(bytes, section_raw_offsets[0] + 40, (u32)(s32)main_displacement);
        link_write_u32(bytes, section_raw_offsets[0] + 48, (u32)(s32)exit_displacement);
    }
    else
    {
        u64 exit_thunk_rva = section_rvas[0] + thunk_offset + (u64)import_count * thunk_entry_size;
        u64 exit_thunk_raw = section_raw_offsets[0] + thunk_offset + (u64)import_count * thunk_entry_size;
        u64 image_base = ((u64)PE_IMAGE_BASE_HIGH << 32) | PE_IMAGE_BASE_LOW;
        u64 exit_iat_address = image_base + exit_iat_rva;
        u64 exit_thunk_address = image_base + exit_thunk_rva;
        u64 page_offset = exit_iat_address & 0xfff;
        bool adrp_valid = true;
        link_write_u32(bytes, exit_thunk_raw, link_aarch64_adrp(16, exit_thunk_address, exit_iat_address, &adrp_valid));
        if (!adrp_valid || page_offset % 8)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(bytes, exit_thunk_raw + 4, 0xf9400000 | ((u32)(page_offset / 8) << 10) | (16 << 5) | 17);
        link_write_u32(bytes, exit_thunk_raw + 8, 0xd61f0220);
        link_write_u32(bytes, exit_thunk_raw + 12, 0xd503201f);
        s64 main_words = ((s64)main_rva - (s64)(entry_rva + 8)) / 4;
        s64 exit_words = ((s64)exit_thunk_rva - (s64)(entry_rva + 12)) / 4;
        if (main_words < -(1 << 25) || main_words >= (1 << 25) || exit_words < -(1 << 25) || exit_words >= (1 << 25))
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(bytes, section_raw_offsets[0] + 8, 0x94000000 | ((u32)main_words & 0x03ffffff));
        link_write_u32(bytes, section_raw_offsets[0] + 12, 0x94000000 | ((u32)exit_words & 0x03ffffff));
    }
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[relocation_index];
        if (relocation->section < OBJECT_SECTION_COUNT && object_section_kind_is_debug((ObjectSectionKind)relocation->section))
        {
            continue;
        }
        if (relocation->section >= OBJECT_SECTION_COUNT || relocation->symbol >= object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section = &object->sections[relocation->section];
        u64 width = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (relocation->offset > section->data.length || width > section->data.length - relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        u64 symbol_rva = 0;
        bool data_import = false;
        if (symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index = import_indices[relocation->symbol];
            bool call_relocation =
                (!aarch64 && relocation->kind == OBJECT_RELOCATION_X86_64_PC32) || (aarch64 && relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26);
            if (import_index == UINT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            if (symbol->kind == OBJECT_SYMBOL_DATA)
            {
                if ((!aarch64 && relocation->kind != OBJECT_RELOCATION_X86_64_PC32) ||
                    (aarch64 && relocation->kind != OBJECT_RELOCATION_ABSOLUTE64))
                {
                    result.error = LINK_ERROR_RELOCATION;
                    result.symbol = symbol->name;
                    return result;
                }
                symbol_rva = import_section_rva + runtime_address_offset + (u64)import_slots[relocation->symbol] * sizeof(u64);
                data_import = true;
            }
            else
            {
                if (!call_relocation)
                {
                    result.error = LINK_ERROR_RELOCATION;
                    result.symbol = symbol->name;
                    return result;
                }
                symbol_rva = section_rvas[0] + thunk_offset + (u64)import_index * thunk_entry_size;
            }
        }
        else if (symbol->section < OBJECT_SECTION_COUNT)
        {
            symbol_rva = section_rvas[object_output_sections[symbol->section]] + object_section_offsets[symbol->section] + symbol->value;
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 place_rva = section_rvas[object_output_sections[relocation->section]] + object_section_offsets[relocation->section] + relocation->offset;
        u64 output_offset = section_raw_offsets[object_output_sections[relocation->section]] + object_section_offsets[relocation->section] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32)
        {
            s64 value = (s64)(import_section_rva + tls_index_offset) + relocation->addend - (s64)place_rva;
            if (value < INT32_MIN || value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)(s32)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_PE_TLS_OFFSET32)
        {
            if (symbol->section != OBJECT_SECTION_THREAD_LOCAL_DATA && symbol->section != OBJECT_SECTION_THREAD_LOCAL_ZERO)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            u64 tls_offset = object_section_offsets[symbol->section] + symbol->value + (u64)relocation->addend;
            if (tls_offset > UINT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)tls_offset);
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP)
        {
            u64 target_address = pe_image_base + import_section_rva + tls_index_offset;
            u64 place_address = pe_image_base + place_rva;
            bool valid_adrp = true;
            u32 encoded = link_aarch64_adrp(9, place_address, target_address, &valid_adrp);
            if (!valid_adrp)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, encoded);
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12)
        {
            u32 page_offset = (u32)((import_section_rva + tls_index_offset) & 0xfff);
            if (page_offset % 4)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, 0xb9400129 | ((page_offset / 4) << 10));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12)
        {
            if (symbol->section != OBJECT_SECTION_THREAD_LOCAL_DATA && symbol->section != OBJECT_SECTION_THREAD_LOCAL_ZERO)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            u64 tls_offset = object_section_offsets[symbol->section] + symbol->value + (u64)relocation->addend;
            if (tls_offset > 4095)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, 0x91000129 | ((u32)tls_offset << 10));
        }
        else if (relocation->kind == OBJECT_RELOCATION_X86_64_PC32)
        {
            if (data_import)
            {
                if (relocation->section != OBJECT_SECTION_TEXT || relocation->offset < 3 || bytes[output_offset - 3] != 0x48 ||
                    bytes[output_offset - 2] != 0x8d || bytes[output_offset - 1] != 0x05)
                {
                    result.error = LINK_ERROR_RELOCATION;
                    result.symbol = symbol->name;
                    return result;
                }
                bytes[output_offset - 2] = 0x8b;
            }
            s64 value = (s64)symbol_rva + relocation->addend - (s64)place_rva;
            if (value < INT32_MIN || value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)(s32)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26)
        {
            s64 displacement = (s64)symbol_rva + relocation->addend - (s64)place_rva;
            s64 words = displacement / 4;
            if (displacement % 4 || words < -(1 << 25) || words >= (1 << 25))
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, 0x94000000 | ((u32)words & 0x03ffffff));
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            u64 image_base = ((u64)PE_IMAGE_BASE_HIGH << 32) | PE_IMAGE_BASE_LOW;
            if (data_import)
            {
                if (!aarch64 || relocation->section != OBJECT_SECTION_TEXT || relocation->offset < 8 ||
                    link_read_u32(bytes + output_offset - 8, 0) != UINT32_C(0x58000049) ||
                    link_read_u32(bytes + output_offset - 4, 0) != UINT32_C(0x14000003) || relocation->addend)
                {
                    result.error = LINK_ERROR_RELOCATION;
                    result.symbol = symbol->name;
                    return result;
                }
                u64 iat_address = image_base + symbol_rva;
                u64 load_address = image_base + place_rva - 8;
                bool valid = true;
                link_write_u32(bytes, output_offset - 8, link_aarch64_adrp(9, load_address, iat_address, &valid));
                u64 page_offset = symbol_rva & 0xfff;
                if (!valid || page_offset % sizeof(u64))
                {
                    result.error = LINK_ERROR_RELOCATION;
                    result.symbol = symbol->name;
                    return result;
                }
                link_write_u32(bytes, output_offset - 4, UINT32_C(0xf9400000) | ((u32)(page_offset / sizeof(u64)) << 10) | (9 << 5) | 9);
                link_write_u32(bytes, output_offset, UINT32_C(0x14000002));
                link_write_u32(bytes, output_offset + sizeof(u32), 0);
            }
            else
            {
                link_write_u64(bytes, output_offset, image_base + symbol_rva + (u64)relocation->addend);
            }
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    bytes[0] = 'M';
    bytes[1] = 'Z';
    link_write_u32(bytes, 0x3c, PE_OFFSET);
    memcpy(bytes + PE_OFFSET, "PE\0\0", 4);
    u64 coff = PE_OFFSET + 4;
    link_write_u16(bytes, coff, aarch64 ? 0xaa64 : 0x8664);
    link_write_u16(bytes, coff + 2, pe_section_count);
    link_write_u16(bytes, coff + 16, PE_OPTIONAL_HEADER_SIZE);
    link_write_u16(bytes, coff + 18, 0x23);
    u64 optional = coff + PE_COFF_HEADER_SIZE;
    link_write_u16(bytes, optional, 0x20b);
    link_write_u32(bytes, optional + 4, (u32)text_raw_size);
    link_write_u32(bytes, optional + 8, (u32)(read_only_raw_size + data_raw_size + tls_raw_size + import_raw_size + (emit_debug ? debug_raw_size : 0)));
    link_write_u32(bytes, optional + 12, (u32)zero_virtual_size);
    link_write_u32(bytes, optional + 16, (u32)entry_rva);
    link_write_u32(bytes, optional + 20, section_rvas[0]);
    link_write_u32(bytes, optional + 24, PE_IMAGE_BASE_LOW);
    link_write_u32(bytes, optional + 28, PE_IMAGE_BASE_HIGH);
    link_write_u32(bytes, optional + 32, PE_SECTION_ALIGNMENT);
    link_write_u32(bytes, optional + 36, PE_FILE_ALIGNMENT);
    link_write_u16(bytes, optional + 40, 6);
    link_write_u16(bytes, optional + 48, 6);
    link_write_u32(bytes, optional + 56, (u32)image_size);
    link_write_u32(bytes, optional + 60, (u32)header_size);
    link_write_u16(bytes, optional + 68, 3);
    link_write_u16(bytes, optional + 70, 0x100);
    link_write_u64(bytes, optional + 72, LINK_PE_STACK_RESERVE);
    link_write_u64(bytes, optional + 80, 4096);
    link_write_u64(bytes, optional + 88, 1024 * 1024);
    link_write_u64(bytes, optional + 96, 4096);
    link_write_u32(bytes, optional + 108, 16);
    link_write_u32(bytes, optional + 120, import_section_rva + (u32)import_descriptor_offset);
    link_write_u32(bytes, optional + 124, (u32)(import_descriptor_count * PE_IMPORT_DESCRIPTOR_SIZE));
    if (tls_virtual_size)
    {
        link_write_u32(bytes, optional + 184, import_section_rva + (u32)tls_directory_offset);
        link_write_u32(bytes, optional + 188, 40);
    }
    link_write_u32(bytes, optional + 208, import_section_rva + (u32)(total_import_slots ? runtime_address_offset : kernel_address_offset));
    link_write_u32(bytes, optional + 212, (u32)(total_import_slots ? ((u64)total_import_slots + 4) * sizeof(u64) : 2 * sizeof(u64)));
    if (emit_debug)
    {
        link_write_u32(bytes, optional + 160, section_rvas[6]);
        link_write_u32(bytes, optional + 164, 28);
    }
    u64 section_header = optional + PE_OPTIONAL_HEADER_SIZE;
    link_pe_section_header(bytes, section_header, ".text\0\0\0", (u32)text_virtual_size, section_rvas[0], (u32)text_raw_size, section_raw_offsets[0],
                           0x60000020);
    link_pe_section_header(bytes, section_header + PE_SECTION_HEADER_SIZE, ".rdata\0\0", (u32)BUSTER_MAX(read_only_virtual_size, 1), section_rvas[1],
                           (u32)read_only_raw_size, section_raw_offsets[1], 0x40000040);
    link_pe_section_header(bytes, section_header + 2 * PE_SECTION_HEADER_SIZE, ".data\0\0\0", (u32)BUSTER_MAX(data_virtual_size, 1), section_rvas[2],
                           (u32)data_raw_size, section_raw_offsets[2], 0xc0000040);
    link_pe_section_header(bytes, section_header + 3 * PE_SECTION_HEADER_SIZE, ".bss\0\0\0\0", (u32)BUSTER_MAX(zero_virtual_size, 1), section_rvas[3], 0, 0,
                           0xc0000080);
    link_pe_section_header(bytes, section_header + 4 * PE_SECTION_HEADER_SIZE, ".tls\0\0\0\0", (u32)BUSTER_MAX(tls_virtual_size, 1), section_rvas[4],
                           (u32)tls_raw_size, section_raw_offsets[4], 0xc0000040);
    link_pe_section_header(bytes, section_header + 5 * PE_SECTION_HEADER_SIZE, ".idata\0\0", (u32)import_virtual_size, section_rvas[5], (u32)import_raw_size,
                           section_raw_offsets[5], 0xc0000040);
    if (emit_debug)
    {
        link_pe_section_header(bytes, section_header + 6 * PE_SECTION_HEADER_SIZE, ".debug\0\0", (u32)debug_virtual_size, section_rvas[6], (u32)debug_raw_size,
                               section_raw_offsets[6], 0x40000040);

        PdbSection* pdb_sections = arena_allocate(arena, PdbSection, PE_SECTION_COUNT);
        for (u32 section_index = 0; section_index < PE_SECTION_COUNT; section_index += 1)
        {
            String8 name = section_index == 0   ? S8(".text")
                          : section_index == 1 ? S8(".rdata")
                          : section_index == 2 ? S8(".data")
                          : section_index == 3 ? S8(".bss")
                          : section_index == 4 ? S8(".tls")
                          : section_index == 5 ? S8(".idata")
                                               : S8(".debug");
            u64 virtual_size = section_index == 0   ? text_virtual_size
                               : section_index == 1 ? read_only_virtual_size
                               : section_index == 2 ? data_virtual_size
                               : section_index == 3 ? zero_virtual_size
                               : section_index == 4 ? tls_virtual_size
                               : section_index == 5 ? import_virtual_size
                                                    : debug_virtual_size;
            u64 raw_size = section_index == 0   ? text_raw_size
                           : section_index == 1 ? read_only_raw_size
                           : section_index == 2 ? data_raw_size
                           : section_index == 3 ? 0
                           : section_index == 4 ? tls_raw_size
                           : section_index == 5 ? import_raw_size
                                                : debug_raw_size;
            u32 characteristics = section_index == 0   ? 0x60000020
                                  : section_index == 1 ? 0x40000040
                                  : section_index == 2 ? 0xc0000040
                                  : section_index == 3 ? 0xc0000080
                                  : section_index == 4 ? 0xc0000040
                                  : section_index == 5 ? 0xc0000040
                                                       : 0x40000040;
            pdb_sections[section_index] = (PdbSection){
                .name = name,
                .virtual_address = section_rvas[section_index],
                .virtual_size = (u32)BUSTER_MIN(virtual_size, UINT32_MAX),
                .raw_size = (u32)BUSTER_MIN(raw_size, UINT32_MAX),
                .raw_offset = section_raw_offsets[section_index],
                .characteristics = characteristics,
            };
        }
        PdbModule* pdb_modules = arena_allocate(arena, PdbModule, object->debug_module_count);
        u64 identity_size = base_file_size;
        for (u32 module_index = 0; module_index < object->debug_module_count; module_index += 1)
        {
            ObjectDebugModule* source = object->debug_modules + module_index;
            ByteSlice symbols =
                link_pe_resolved_codeview(arena, object, source, object_output_sections, object_section_offsets, pe_section_count);
            if (!symbols.pointer || source->types_offset > object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data.length ||
                source->types_size > object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data.length - source->types_offset)
            {
                result.error = LINK_ERROR_OBJECT_WRITE;
                return result;
            }
            u8* types = arena_allocate(arena, u8, source->types_size);
            if (source->types_size)
            {
                memcpy(types, object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data.pointer + source->types_offset, source->types_size);
            }
            pdb_modules[module_index] = (PdbModule){
                .name = source->name,
                .codeview_symbols = symbols,
                .codeview_types = (ByteSlice){.pointer = types, .length = source->types_size},
                .code_offset = (u32)(object_section_offsets[OBJECT_SECTION_TEXT] + source->code_offset),
                .code_size = (u32)BUSTER_MIN(source->code_size, UINT32_MAX),
                .code_section = 1,
            };
            identity_size += source->name.length + symbols.length + source->types_size + 16;
        }
        u8* identity = arena_allocate(arena, u8, identity_size);
        memcpy(identity, bytes, base_file_size);
        u64 identity_cursor = base_file_size;
        for (u32 module_index = 0; module_index < object->debug_module_count; module_index += 1)
        {
            PdbModule* module = pdb_modules + module_index;
            memcpy(identity + identity_cursor, module->name.pointer, module->name.length);
            identity_cursor += module->name.length;
            memcpy(identity + identity_cursor, module->codeview_symbols.pointer, module->codeview_symbols.length);
            identity_cursor += module->codeview_symbols.length;
            memcpy(identity + identity_cursor, module->codeview_types.pointer, module->codeview_types.length);
            identity_cursor += module->codeview_types.length;
            link_write_u32(identity, identity_cursor, module->code_offset);
            identity_cursor += 4;
            link_write_u32(identity, identity_cursor, module->code_size);
            identity_cursor += 4;
        }
        u8* digest = arena_allocate(arena, u8, 32);
        memset(digest, 0, 32);
        link_sha256(arena, identity, identity_cursor, digest);
        PdbInput pdb_input = {
            .sections = pdb_sections,
            .section_count = PE_SECTION_COUNT,
            .age = 1,
            .machine = aarch64 ? 0xaa64 : 0x8664,
            .modules = pdb_modules,
            .module_count = object->debug_module_count,
        };
        memcpy(pdb_input.guid, digest, sizeof(pdb_input.guid));
        PdbResult pdb = pdb_build(arena, pdb_input);
        if (!pdb.valid)
        {
            result.error = LINK_ERROR_OBJECT_WRITE;
            return result;
        }
        result.pdb = pdb.bytes;
        result.pdb_path = pdb_path;
        u64 debug_raw = section_raw_offsets[5];
        u32 rsds_size = (u32)(24 + pdb_path.length + 1);
        link_write_u32(bytes, debug_raw, 0);
        link_write_u32(bytes, debug_raw + 4, 0);
        link_write_u16(bytes, debug_raw + 8, 0);
        link_write_u16(bytes, debug_raw + 10, 0);
        link_write_u32(bytes, debug_raw + 12, 2);
        link_write_u32(bytes, debug_raw + 16, rsds_size);
        link_write_u32(bytes, debug_raw + 20, section_rvas[5] + 28);
        link_write_u32(bytes, debug_raw + 24, (u32)(debug_raw + 28));
        memcpy(bytes + debug_raw + 28, "RSDS", 4);
        memcpy(bytes + debug_raw + 32, pdb_input.guid, sizeof(pdb_input.guid));
        link_write_u32(bytes, debug_raw + 48, 1);
        memcpy(bytes + debug_raw + 52, pdb_path.pointer, pdb_path.length);
        bytes[debug_raw + 52 + pdb_path.length] = 0;
    }
    if (options.output_path.length && !link_write_executable_file(options.output_path, result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    else if (emit_debug && options.output_path.length && !link_write_executable_file(result.pdb_path, result.pdb))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void link_mach_name_write(u8* bytes, u64 offset, char const* name)
{
    u64 length = strlen(name);
    memcpy(bytes + offset, name, BUSTER_MIN(length, 16));
}

BUSTER_GLOBAL_LOCAL u64 link_uleb128_write(u8* bytes, u64 offset, u64 value)
{
    do
    {
        u8 byte = (u8)(value & 0x7f);
        value >>= 7;
        if (value)
        {
            byte |= 0x80;
        }
        bytes[offset++] = byte;
    } while (value);
    return offset;
}

BUSTER_GLOBAL_LOCAL void link_mach_section_write(u8* bytes, u64 offset, char const* section_name, char const* segment_name, u64 address, u64 size,
                                                 u32 file_offset, u32 alignment, u32 flags, u32 reserved1, u32 reserved2)
{
    link_mach_name_write(bytes, offset, section_name);
    link_mach_name_write(bytes, offset + 16, segment_name);
    link_write_u64(bytes, offset + 32, address);
    link_write_u64(bytes, offset + 40, size);
    link_write_u32(bytes, offset + 48, file_offset);
    link_write_u32(bytes, offset + 52, alignment);
    link_write_u32(bytes, offset + 64, flags);
    link_write_u32(bytes, offset + 68, reserved1);
    link_write_u32(bytes, offset + 72, reserved2);
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult link_native_executable_mach_o64(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        MACH_HEADER_SIZE = 32,
        MACH_SEGMENT_COMMAND_SIZE = 72,
        MACH_SECTION_SIZE = 80,
        MACH_DYLD_INFO_COMMAND_SIZE = 48,
        MACH_DYLINKER_COMMAND_SIZE = 32,
        MACH_DYLIB_COMMAND_SIZE = 56,
        MACH_MAIN_COMMAND_SIZE = 24,
        MACH_BUILD_VERSION_COMMAND_SIZE = 24,
        MACH_SYMTAB_COMMAND_SIZE = 24,
        MACH_DYSYMTAB_COMMAND_SIZE = 80,
        MACH_CODE_SIGNATURE_COMMAND_SIZE = 16,
        MACH_BASE_COMMAND_COUNT = 12,
        MACH_PAGE_SIZE = 0x4000,
        MACH_CODE_PAGE_SIZE = 0x1000,
        MACH_CODE_DIRECTORY_HEADER_SIZE = 48,
        MACH_CODE_DIRECTORY_IDENTIFIER_SIZE = 7,
        MACH_NLIST_SIZE = 16,
        MACH_STUB_X86_64_SIZE = 6,
        MACH_STUB_AARCH64_SIZE = 16,
    };
    static char8 const dyld_path[] = "/usr/lib/dyld";
    static char8 const system_path[] = "/usr/lib/libSystem.B.dylib";
    if ((options.dynamic_library_count && !options.dynamic_libraries) || object->section_count < OBJECT_SECTION_COUNT || !object->sections ||
        (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations) ||
        (object->target.cpu_arch != CPU_ARCH_X86_64 && object->target.cpu_arch != CPU_ARCH_AARCH64))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u32 import_count = 0;
    u64 import_name_size = 0;
    u32 thread_local_count = 0;
    u32 bootstrap_symbol_index = UINT32_MAX;
    u32* import_indices = arena_allocate(arena, u32, object->symbol_count);
    u32* thread_local_indices = arena_allocate(arena, u32, object->symbol_count);
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        thread_local_indices[symbol_index] = UINT32_MAX;
        ObjectSymbol* symbol = &object->symbols[symbol_index];
        if (symbol->section != OBJECT_SECTION_UNDEFINED)
        {
            if (symbol->section < object->section_count && (object->sections[symbol->section].kind == OBJECT_SECTION_THREAD_LOCAL_DATA ||
                                                            object->sections[symbol->section].kind == OBJECT_SECTION_THREAD_LOCAL_ZERO))
            {
                thread_local_indices[symbol_index] = thread_local_count++;
            }
            continue;
        }
        if (!symbol->global || symbol->kind != OBJECT_SYMBOL_FUNCTION || !symbol->name.length)
        {
            result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = symbol->name;
            return result;
        }
        import_indices[symbol_index] = import_count++;
        if (string_equal(symbol->name, S8("_tlv_bootstrap")))
        {
            bootstrap_symbol_index = symbol_index;
        }
        import_name_size += symbol->name.length + 2;
    }
    if (thread_local_count && bootstrap_symbol_index == UINT32_MAX)
    {
        result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
        result.symbol = S8("_tlv_bootstrap");
        return result;
    }
    String8 entry_name = options.entry_symbol.length ? options.entry_symbol : S8("main");
    u32 entry_symbol_index = link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX || object->symbols[entry_symbol_index].section == OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind != OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    u32 data_section_count = 4 + (thread_local_count ? 3 : 0);
    u64 data_command_size = MACH_SEGMENT_COMMAND_SIZE + (u64)data_section_count * MACH_SECTION_SIZE;
    bool has_unwind = object->sections[OBJECT_SECTION_UNWIND].data.length != 0;
    u64 text_command_size = MACH_SEGMENT_COMMAND_SIZE + (has_unwind ? 2 : 1) * MACH_SECTION_SIZE;
    // Debug sections travel in their own read-only __DWARF segment.  The
    // segment is file-backed and page-rounded (rather than unmapped), which
    // keeps the image acceptable to dyld while retaining the section names
    // that source debuggers expect.
    static ObjectSectionKind const dwarf_kinds[] = {
        OBJECT_SECTION_DEBUG_INFO,
        OBJECT_SECTION_DEBUG_ABBREV,
        OBJECT_SECTION_DEBUG_LINE,
        OBJECT_SECTION_DEBUG_STR,
        OBJECT_SECTION_DEBUG_LOC,
        OBJECT_SECTION_DEBUG_RANGES,
    };
    static char const* const dwarf_section_names[] = {
        "__debug_info",
        "__debug_abbrev",
        "__debug_line",
        "__debug_str",
        "__debug_loc",
        "__debug_ranges",
    };
    u64 dwarf_total = 0;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(dwarf_kinds); index += 1)
    {
        dwarf_total += object->sections[dwarf_kinds[index]].data.length;
    }
    bool has_dwarf = dwarf_total != 0;
    u64 dwarf_command_size = has_dwarf ? MACH_SEGMENT_COMMAND_SIZE + BUSTER_ARRAY_LENGTH(dwarf_kinds) * MACH_SECTION_SIZE : 0;
    u64 extra_dylib_command_size = 0;
    for (u32 library_index = 0; library_index < options.dynamic_library_count; library_index += 1)
    {
        String8 library = options.dynamic_libraries[library_index].name;
        if (!library.length || library.length > UINT32_MAX - 25)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        extra_dylib_command_size += align_forward(24 + library.length + 1, 8);
    }
    u64 command_size = 2 * MACH_SEGMENT_COMMAND_SIZE + text_command_size + data_command_size + dwarf_command_size + MACH_DYLD_INFO_COMMAND_SIZE +
                       MACH_DYLINKER_COMMAND_SIZE + MACH_DYLIB_COMMAND_SIZE + extra_dylib_command_size + MACH_MAIN_COMMAND_SIZE +
                       MACH_BUILD_VERSION_COMMAND_SIZE + MACH_SYMTAB_COMMAND_SIZE + MACH_DYSYMTAB_COMMAND_SIZE + MACH_CODE_SIGNATURE_COMMAND_SIZE;
    u64 header_end = MACH_HEADER_SIZE + command_size;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    section_offsets[OBJECT_SECTION_TEXT] = align_forward(header_end, object->sections[OBJECT_SECTION_TEXT].alignment);
    u32 stub_size = object->target.cpu_arch == CPU_ARCH_X86_64 ? MACH_STUB_X86_64_SIZE : MACH_STUB_AARCH64_SIZE;
    u64 stub_offset = align_forward(section_offsets[OBJECT_SECTION_TEXT] + object->sections[OBJECT_SECTION_TEXT].data.length, 16);
    u64 stub_end = stub_offset + (u64)import_count * stub_size;
    section_offsets[OBJECT_SECTION_UNWIND] = has_unwind ? align_forward(stub_end, object->sections[OBJECT_SECTION_UNWIND].alignment) : stub_end;
    u64 text_end = has_unwind ? section_offsets[OBJECT_SECTION_UNWIND] + object->sections[OBJECT_SECTION_UNWIND].data.length : stub_end;
    u64 data_file_offset = align_forward(text_end, MACH_PAGE_SIZE);
    section_offsets[OBJECT_SECTION_DATA] = data_file_offset;
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] =
        align_forward(section_offsets[OBJECT_SECTION_DATA] + object->sections[OBJECT_SECTION_DATA].data.length,
                      object->sections[OBJECT_SECTION_READ_ONLY_DATA].alignment);
    u64 got_offset = align_forward(section_offsets[OBJECT_SECTION_READ_ONLY_DATA] + object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length, 8);
    u64 got_count = (u64)import_count + thread_local_count;
    u64 thread_local_variables_offset = align_forward(got_offset + got_count * sizeof(u64), 8);
    u64 thread_local_variables_size = (u64)thread_local_count * 3 * sizeof(u64);
    section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA] =
        align_forward(thread_local_variables_offset + thread_local_variables_size, object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].alignment);
    u64 data_end = section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA] + object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length;
    section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO] = align_forward(data_end, object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].alignment);
    u64 thread_local_vm_end = section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO] + object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size;
    section_offsets[OBJECT_SECTION_ZERO] = align_forward(thread_local_vm_end, object->sections[OBJECT_SECTION_ZERO].alignment);
    u64 data_vm_end = section_offsets[OBJECT_SECTION_ZERO] + object->sections[OBJECT_SECTION_ZERO].virtual_size;
    // The kernel loader rejects an image whose segments are not page aligned
    // in the file. Zero-fill storage advances the virtual layout without
    // consuming file bytes, so later segments need distinct file and virtual
    // offsets.
    u64 data_file_end = BUSTER_MAX(data_end, data_file_offset + 1);
    u64 dwarf_file_offset = align_forward(data_file_end, MACH_PAGE_SIZE);
    u64 dwarf_vm_offset = align_forward(BUSTER_MAX(data_vm_end, data_file_offset + 1), MACH_PAGE_SIZE);
    u64* dwarf_file_offsets = arena_allocate(arena, u64, BUSTER_ARRAY_LENGTH(dwarf_kinds));
    u64* dwarf_vm_offsets = arena_allocate(arena, u64, BUSTER_ARRAY_LENGTH(dwarf_kinds));
    u64 dwarf_file_cursor = dwarf_file_offset;
    u64 dwarf_vm_cursor = dwarf_vm_offset;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(dwarf_kinds); index += 1)
    {
        dwarf_file_offsets[index] = dwarf_file_cursor;
        dwarf_vm_offsets[index] = dwarf_vm_cursor;
        dwarf_file_cursor += object->sections[dwarf_kinds[index]].data.length;
        dwarf_vm_cursor += object->sections[dwarf_kinds[index]].data.length;
    }
    u64 linkedit_file_offset = align_forward(has_dwarf ? dwarf_file_cursor : data_file_end, MACH_PAGE_SIZE);
    u64 linkedit_vm_offset = align_forward(has_dwarf ? dwarf_vm_cursor : BUSTER_MAX(data_vm_end, data_file_offset + 1), MACH_PAGE_SIZE);
    u32 rebase_count = 0;
    for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
    {
        ObjectRelocation relocation = object->relocations[relocation_index];
        if (relocation.section < OBJECT_SECTION_COUNT && object_section_kind_is_debug((ObjectSectionKind)relocation.section))
        {
            continue;
        }
        if (relocation.kind == OBJECT_RELOCATION_ABSOLUTE64 && !(object->target.cpu_arch == CPU_ARCH_AARCH64 && relocation.section == OBJECT_SECTION_TEXT))
        {
            rebase_count += 1;
        }
    }
    rebase_count += thread_local_count * 2;
    u64 rebase_capacity = rebase_count ? 2 + (u64)rebase_count * 12 : 0;
    u8* rebase_bytes = arena_allocate(arena, u8, rebase_capacity);
    u64 rebase_size = 0;
    if (rebase_count)
    {
        rebase_bytes[rebase_size++] = 0x11;
        for (u32 relocation_index = 0; relocation_index < object->relocation_count; relocation_index += 1)
        {
            ObjectRelocation* relocation = &object->relocations[relocation_index];
            if (relocation->kind != OBJECT_RELOCATION_ABSOLUTE64 || (object->target.cpu_arch == CPU_ARCH_AARCH64 && relocation->section == OBJECT_SECTION_TEXT) ||
                (relocation->section < OBJECT_SECTION_COUNT && object_section_kind_is_debug((ObjectSectionKind)relocation->section)))
            {
                continue;
            }
            if (relocation->section >= OBJECT_SECTION_COUNT)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            u8 segment_index = relocation->section == OBJECT_SECTION_DATA || relocation->section == OBJECT_SECTION_ZERO ||
                                       relocation->section == OBJECT_SECTION_READ_ONLY_DATA ||
                                       relocation->section == OBJECT_SECTION_THREAD_LOCAL_DATA || relocation->section == OBJECT_SECTION_THREAD_LOCAL_ZERO
                                   ? 2
                                   : 1;
            u64 segment_offset = segment_index == 2 ? section_offsets[relocation->section] + relocation->offset - data_file_offset
                                                    : section_offsets[relocation->section] + relocation->offset;
            rebase_bytes[rebase_size++] = (u8)(0x20 | segment_index);
            rebase_size = link_uleb128_write(rebase_bytes, rebase_size, segment_offset);
            rebase_bytes[rebase_size++] = 0x51;
        }
        for (u32 thread_local_index = 0; thread_local_index < thread_local_count; thread_local_index += 1)
        {
            u64 pointer_offset = got_offset + ((u64)import_count + thread_local_index) * sizeof(u64);
            u64 descriptor_offset = thread_local_variables_offset + (u64)thread_local_index * 3 * sizeof(u64);
            u64 locations[] = {
                pointer_offset,
                descriptor_offset,
            };
            for (u32 location_index = 0; location_index < BUSTER_ARRAY_LENGTH(locations); location_index += 1)
            {
                rebase_bytes[rebase_size++] = 0x22;
                rebase_size = link_uleb128_write(rebase_bytes, rebase_size, locations[location_index] - data_file_offset);
                rebase_bytes[rebase_size++] = 0x51;
            }
        }
        rebase_bytes[rebase_size++] = 0;
    }
    u64 bind_capacity = import_name_size + (u64)import_count * 20 + 4;
    u8* bind_bytes = arena_allocate(arena, u8, bind_capacity);
    u64 bind_size = 0;
    if (import_count)
    {
        bind_bytes[bind_size++] = options.dynamic_library_count ? 0x3e : 0x11;
        bind_bytes[bind_size++] = 0x51;
        bind_bytes[bind_size++] = 0x72;
        bind_size = link_uleb128_write(bind_bytes, bind_size, got_offset - data_file_offset);
        for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
        {
            if (import_indices[symbol_index] == UINT32_MAX)
            {
                continue;
            }
            ObjectSymbol* symbol = &object->symbols[symbol_index];
            bind_bytes[bind_size++] = 0x40;
            bind_bytes[bind_size++] = '_';
            memcpy(bind_bytes + bind_size, symbol->name.pointer, symbol->name.length);
            bind_size += symbol->name.length;
            bind_bytes[bind_size++] = 0;
            bind_bytes[bind_size++] = 0x90;
        }
        bind_bytes[bind_size++] = 0;
    }
    u64 bind_offset = linkedit_file_offset + rebase_size;
    u64 symbol_table_offset = align_forward(bind_offset + bind_size, 8);
    u64 symbol_table_size = (u64)import_count * MACH_NLIST_SIZE;
    u64 string_table_offset = symbol_table_offset + symbol_table_size;
    u64 string_table_size = 1 + import_name_size;
    u64 unsigned_file_size = string_table_offset + string_table_size;
    u64 signature_offset = align_forward(unsigned_file_size, 16);
    u64 code_slot_count = (signature_offset + MACH_CODE_PAGE_SIZE - 1) / MACH_CODE_PAGE_SIZE;
    u64 code_directory_size = MACH_CODE_DIRECTORY_HEADER_SIZE + MACH_CODE_DIRECTORY_IDENTIFIER_SIZE + code_slot_count * 32;
    u64 signature_size = 20 + code_directory_size;
    u64 file_size = signature_offset + signature_size;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(arena, u8, file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    for (u32 section = 0; section < OBJECT_SECTION_COUNT; section += 1)
    {
        if (object_section_kind_is_debug((ObjectSectionKind)section))
        {
            continue;
        }
        ByteSlice data = object->sections[section].data;
        if (data.length)
        {
            memcpy(bytes + section_offsets[section], data.pointer, data.length);
        }
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(dwarf_kinds); index += 1)
    {
        ByteSlice data = object->sections[dwarf_kinds[index]].data;
        if (data.length)
        {
            memcpy(bytes + dwarf_file_offsets[index], data.pointer, data.length);
        }
    }
    if (rebase_size)
    {
        memcpy(bytes + linkedit_file_offset, rebase_bytes, rebase_size);
    }
    if (bind_size)
    {
        memcpy(bytes + bind_offset, bind_bytes, bind_size);
    }
    u64 image_base = UINT64_C(0x100000000);
    u64 text_vm_address = image_base;
    u64 data_vm_address = image_base + data_file_offset;
    u64 string_cursor = string_table_offset + 1;
    u32 string_index = 1;
    bool valid = true;
    for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
    {
        u32 import_index = import_indices[symbol_index];
        if (import_index == UINT32_MAX)
        {
            continue;
        }
        ObjectSymbol* symbol = &object->symbols[symbol_index];
        u64 nlist = symbol_table_offset + (u64)import_index * MACH_NLIST_SIZE;
        link_write_u32(bytes, nlist, string_index);
        bytes[nlist + 4] = 0x01;
        link_write_u16(bytes, nlist + 6, 0x0100);
        bytes[string_cursor++] = '_';
        memcpy(bytes + string_cursor, symbol->name.pointer, symbol->name.length);
        string_cursor += symbol->name.length + 1;
        string_index += (u32)symbol->name.length + 2;
        u64 slot_address = data_vm_address + (got_offset - data_file_offset) + (u64)import_index * sizeof(u64);
        u64 current_stub_offset = stub_offset + (u64)import_index * stub_size;
        u64 stub_address = text_vm_address + current_stub_offset;
        if (object->target.cpu_arch == CPU_ARCH_X86_64)
        {
            s64 displacement = (s64)slot_address - (s64)(stub_address + 6);
            if (displacement < INT32_MIN || displacement > INT32_MAX)
            {
                valid = false;
                break;
            }
            bytes[current_stub_offset] = 0xff;
            bytes[current_stub_offset + 1] = 0x25;
            link_write_u32(bytes, current_stub_offset + 2, (u32)(s32)displacement);
        }
        else
        {
            u64 page_offset = slot_address & 0xfff;
            if (page_offset % 8)
            {
                valid = false;
                break;
            }
            link_write_u32(bytes, current_stub_offset, link_aarch64_adrp(16, stub_address, slot_address, &valid));
            link_write_u32(bytes, current_stub_offset + 4, 0xf9400000 | ((u32)(page_offset / 8) << 10) | (16 << 5) | 17);
            link_write_u32(bytes, current_stub_offset + 8, 0xd61f0220);
            link_write_u32(bytes, current_stub_offset + 12, 0xd503201f);
        }
    }
    if (!valid)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    if (thread_local_count)
    {
        u32 bootstrap_import_index = import_indices[bootstrap_symbol_index];
        if (bootstrap_import_index == UINT32_MAX)
        {
            result.error = LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = S8("_tlv_bootstrap");
            return result;
        }
        u64 bootstrap_address = text_vm_address + stub_offset + (u64)bootstrap_import_index * stub_size;
        for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
        {
            u32 thread_local_index = thread_local_indices[symbol_index];
            if (thread_local_index == UINT32_MAX)
            {
                continue;
            }
            ObjectSymbol* symbol = &object->symbols[symbol_index];
            u64 descriptor_offset = thread_local_variables_offset + (u64)thread_local_index * 3 * sizeof(u64);
            u64 descriptor_address = image_base + descriptor_offset;
            u64 initializer_offset = symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO
                                         ? section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO] - section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA] + symbol->value
                                         : symbol->value;
            u64 pointer_offset = got_offset + ((u64)import_count + thread_local_index) * sizeof(u64);
            link_write_u64(bytes, pointer_offset, descriptor_address);
            link_write_u64(bytes, descriptor_offset, bootstrap_address);
            link_write_u64(bytes, descriptor_offset + 2 * sizeof(u64), initializer_offset);
        }
    }
    // Debug sections are not part of the executable's load policy, so their
    // relocations are resolved here rather than through dyld: address slots
    // take link-time virtual addresses, range/location text slots stay
    // section-relative DWARF 4 offsets, and 32-bit slots take offsets into the
    // target debug section.
    for (u32 index = 0; has_dwarf && index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[index];
        if (relocation->section >= OBJECT_SECTION_COUNT || !object_section_kind_is_debug((ObjectSectionKind)relocation->section) ||
            relocation->symbol >= object->symbol_count)
        {
            continue;
        }
        u32 debug_index = 0;
        while (debug_index < BUSTER_ARRAY_LENGTH(dwarf_kinds) && dwarf_kinds[debug_index] != (ObjectSectionKind)relocation->section)
        {
            debug_index += 1;
        }
        if (debug_index >= BUSTER_ARRAY_LENGTH(dwarf_kinds))
        {
            continue;
        }
        ObjectSection* section = &object->sections[relocation->section];
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        u64 width = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (relocation->offset > section->data.length || width > section->data.length - relocation->offset || symbol->section >= OBJECT_SECTION_COUNT)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        u64 slot = dwarf_file_offsets[debug_index] + relocation->offset;
        if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            bool relative_text_range = (relocation->section == OBJECT_SECTION_DEBUG_LOC || relocation->section == OBJECT_SECTION_DEBUG_RANGES) &&
                                       symbol->section == OBJECT_SECTION_TEXT;
            s64 value = (s64)symbol->value + relocation->addend;
            if (!relative_text_range)
            {
                value += (s64)image_base + (s64)section_offsets[symbol->section];
            }
            link_write_u64(bytes, slot, (u64)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE32 && object_section_kind_is_debug((ObjectSectionKind)symbol->section))
        {
            link_write_u32(bytes, slot, (u32)((s64)symbol->value + relocation->addend));
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    for (u32 index = 0; index < object->relocation_count; index += 1)
    {
        ObjectRelocation* relocation = &object->relocations[index];
        if (relocation->section < OBJECT_SECTION_COUNT && object_section_kind_is_debug((ObjectSectionKind)relocation->section))
        {
            continue;
        }
        if (relocation->section >= OBJECT_SECTION_COUNT || relocation->symbol >= object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section = &object->sections[relocation->section];
        u64 width = relocation->kind == OBJECT_RELOCATION_ABSOLUTE64 ? 8 : 4;
        if (relocation->offset > section->data.length || width > section->data.length - relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol = &object->symbols[relocation->symbol];
        u64 symbol_address = 0;
        bool thread_local_pointer = relocation->kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 ||
                                    relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 ||
                                    relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12;
        if (thread_local_pointer)
        {
            u32 thread_local_index = thread_local_indices[relocation->symbol];
            if (thread_local_index == UINT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_address = image_base + got_offset + ((u64)import_count + thread_local_index) * sizeof(u64);
        }
        else if (symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index = import_indices[relocation->symbol];
            if (import_index == UINT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_address = text_vm_address + stub_offset + (u64)import_index * stub_size;
        }
        else if (symbol->section < OBJECT_SECTION_COUNT)
        {
            symbol_address = image_base + section_offsets[symbol->section] + symbol->value;
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 output_offset = section_offsets[relocation->section] + relocation->offset;
        u64 place_address = image_base + output_offset;
        if (relocation->kind == OBJECT_RELOCATION_X86_64_PC32 || relocation->kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 ||
            relocation->kind == OBJECT_RELOCATION_AARCH64_PREL32)
        {
            s64 value = (s64)symbol_address + relocation->addend - (s64)place_address;
            if (value < INT32_MIN || value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, (u32)(s32)value);
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_CALL26)
        {
            s64 displacement = (s64)symbol_address + relocation->addend - (s64)place_address;
            s64 words = displacement / 4;
            if (displacement % 4 || words < -(1 << 25) || words >= (1 << 25))
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(bytes, output_offset, 0x94000000 | ((u32)words & 0x03ffffff));
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21)
        {
            u32 instruction = 0;
            memcpy(&instruction, bytes + output_offset, sizeof(instruction));
            u32 destination = instruction & 31;
            link_write_u32(bytes, output_offset, link_aarch64_adrp(destination, place_address, symbol_address + (u64)relocation->addend, &valid));
            if (!valid)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
        }
        else if (relocation->kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12)
        {
            u64 address = symbol_address + (u64)relocation->addend;
            u64 page_offset = address & 0xfff;
            if (page_offset % sizeof(u64))
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            u32 instruction = 0;
            memcpy(&instruction, bytes + output_offset, sizeof(instruction));
            instruction &= ~(UINT32_C(0xfff) << 10);
            instruction |= (u32)(page_offset / sizeof(u64)) << 10;
            link_write_u32(bytes, output_offset, instruction);
        }
        else if (relocation->kind == OBJECT_RELOCATION_ABSOLUTE64)
        {
            u64 address = symbol_address + (u64)relocation->addend;
            if (object->target.cpu_arch == CPU_ARCH_AARCH64 && relocation->section == OBJECT_SECTION_TEXT)
            {
                if (relocation->offset < 8)
                {
                    result.error = LINK_ERROR_RELOCATION;
                    return result;
                }
                u64 load_offset = output_offset - 8;
                u64 branch_offset = output_offset - 4;
                u32 load = 0;
                u32 branch = 0;
                memcpy(&load, bytes + load_offset, sizeof(load));
                memcpy(&branch, bytes + branch_offset, sizeof(branch));
                if ((load & UINT32_C(0xffffffe0)) != UINT32_C(0x58000040) || branch != UINT32_C(0x14000003))
                {
                    result.error = LINK_ERROR_RELOCATION;
                    return result;
                }
                u32 destination = load & 31;
                u64 load_address = image_base + load_offset;
                link_write_u32(bytes, load_offset, link_aarch64_adrp(destination, load_address, address, &valid));
                link_write_u32(bytes, branch_offset, UINT32_C(0x91000000) | ((u32)(address & 0xfff) << 10) | (destination << 5) | destination);
                link_write_u32(bytes, output_offset, UINT32_C(0x14000002));
                link_write_u32(bytes, output_offset + sizeof(u32), 0);
                if (!valid)
                {
                    result.error = LINK_ERROR_RELOCATION;
                    return result;
                }
            }
            else
            {
                link_write_u64(bytes, output_offset, address);
            }
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    link_write_u32(bytes, 0, 0xfeedfacf);
    link_write_u32(bytes, 4, object->target.cpu_arch == CPU_ARCH_X86_64 ? 0x01000007 : 0x0100000c);
    link_write_u32(bytes, 8, object->target.cpu_arch == CPU_ARCH_X86_64 ? 3 : 0);
    link_write_u32(bytes, 12, 2);
    link_write_u32(bytes, 16, MACH_BASE_COMMAND_COUNT + options.dynamic_library_count + (has_dwarf ? 1 : 0));
    link_write_u32(bytes, 20, (u32)command_size);
    link_write_u32(bytes, 24, 0x200084 | (thread_local_count ? 0x800000 : 0));
    u64 command = MACH_HEADER_SIZE;
#define BUSTER_LINK_MACH_SEGMENT(name, vm_address, vm_size, file_offset, file_segment_size, maximum, initial)                                                  \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        link_write_u32(bytes, command, 0x19);                                                                                                                  \
        link_write_u32(bytes, command + 4, MACH_SEGMENT_COMMAND_SIZE);                                                                                         \
        link_mach_name_write(bytes, command + 8, (name));                                                                                                      \
        link_write_u64(bytes, command + 24, (vm_address));                                                                                                     \
        link_write_u64(bytes, command + 32, (vm_size));                                                                                                        \
        link_write_u64(bytes, command + 40, (file_offset));                                                                                                    \
        link_write_u64(bytes, command + 48, (file_segment_size));                                                                                              \
        link_write_u32(bytes, command + 56, (maximum));                                                                                                        \
        link_write_u32(bytes, command + 60, (initial));                                                                                                        \
        command += MACH_SEGMENT_COMMAND_SIZE;                                                                                                                  \
    } while (0)
    BUSTER_LINK_MACH_SEGMENT("__PAGEZERO", 0, image_base, 0, 0, 0, 0);
    link_write_u32(bytes, command, 0x19);
    link_write_u32(bytes, command + 4, (u32)text_command_size);
    link_mach_name_write(bytes, command + 8, "__TEXT");
    link_write_u64(bytes, command + 24, image_base);
    link_write_u64(bytes, command + 32, data_file_offset);
    link_write_u64(bytes, command + 48, data_file_offset);
    link_write_u32(bytes, command + 56, 5);
    link_write_u32(bytes, command + 60, 5);
    link_write_u32(bytes, command + 64, has_unwind ? 2 : 1);
    link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE, "__text", "__TEXT", image_base + section_offsets[OBJECT_SECTION_TEXT],
                            stub_end - section_offsets[OBJECT_SECTION_TEXT], (u32)section_offsets[OBJECT_SECTION_TEXT], 4, 0x80000400, 0, 0);
    if (has_unwind)
    {
        link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + MACH_SECTION_SIZE, "__eh_frame", "__TEXT",
                                image_base + section_offsets[OBJECT_SECTION_UNWIND], object->sections[OBJECT_SECTION_UNWIND].data.length,
                                (u32)section_offsets[OBJECT_SECTION_UNWIND], 3, 0x6800000b, 0, 0);
    }
    command += text_command_size;
    link_write_u32(bytes, command, 0x19);
    link_write_u32(bytes, command + 4, (u32)data_command_size);
    link_mach_name_write(bytes, command + 8, "__DATA");
    link_write_u64(bytes, command + 24, data_vm_address);
    link_write_u64(bytes, command + 32, data_vm_end - data_file_offset);
    link_write_u64(bytes, command + 40, data_file_offset);
    link_write_u64(bytes, command + 48, data_end - data_file_offset);
    link_write_u32(bytes, command + 56, 3);
    link_write_u32(bytes, command + 60, 3);
    link_write_u32(bytes, command + 64, data_section_count);
    link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE, "__data", "__DATA", data_vm_address, object->sections[OBJECT_SECTION_DATA].data.length,
                            (u32)data_file_offset, 3, 0, 0, 0);
    u32 read_only_alignment_power = 0;
    for (u32 read_only_alignment = object->sections[OBJECT_SECTION_READ_ONLY_DATA].alignment; read_only_alignment > 1; read_only_alignment >>= 1)
    {
        read_only_alignment_power += 1;
    }
    link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + MACH_SECTION_SIZE, "__const", "__DATA",
                            image_base + section_offsets[OBJECT_SECTION_READ_ONLY_DATA], object->sections[OBJECT_SECTION_READ_ONLY_DATA].data.length,
                            (u32)section_offsets[OBJECT_SECTION_READ_ONLY_DATA],
                            read_only_alignment_power, 0, 0, 0);
    link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + 2 * MACH_SECTION_SIZE, "__got", "__DATA", image_base + got_offset, got_count * sizeof(u64),
                            (u32)got_offset, 3, 0, 0, 0);
    if (thread_local_count)
    {
        link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + 3 * MACH_SECTION_SIZE, "__thread_vars", "__DATA",
                                image_base + thread_local_variables_offset, thread_local_variables_size, (u32)thread_local_variables_offset, 3, 0x13, 0, 0);
        link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + 4 * MACH_SECTION_SIZE, "__thread_data", "__DATA",
                                image_base + section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA], object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length,
                                (u32)section_offsets[OBJECT_SECTION_THREAD_LOCAL_DATA], 4, 0x11, 0, 0);
        link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + 5 * MACH_SECTION_SIZE, "__thread_bss", "__DATA",
                                image_base + section_offsets[OBJECT_SECTION_THREAD_LOCAL_ZERO], object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size,
                                0, 4, 0x12, 0, 0);
    }
    u32 zero_alignment_power = 0;
    for (u32 zero_alignment = object->sections[OBJECT_SECTION_ZERO].alignment; zero_alignment > 1; zero_alignment >>= 1)
    {
        zero_alignment_power += 1;
    }
    u32 zero_section_index = 3 + (thread_local_count ? 3 : 0);
    link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + (u64)zero_section_index * MACH_SECTION_SIZE, "__bss", "__DATA",
                            image_base + section_offsets[OBJECT_SECTION_ZERO], object->sections[OBJECT_SECTION_ZERO].virtual_size, 0, zero_alignment_power, 1, 0, 0);
    command += data_command_size;
    if (has_dwarf)
    {
        // dyld refuses a segment whose filesize exceeds its vmsize, so the
        // debug segment is a read-only mapping between __DATA and __LINKEDIT
        // rather than an unmapped one. It is never touched at run time.
        link_write_u32(bytes, command, 0x19);
        link_write_u32(bytes, command + 4, (u32)dwarf_command_size);
        link_mach_name_write(bytes, command + 8, "__DWARF");
        link_write_u64(bytes, command + 24, image_base + dwarf_vm_offset);
        link_write_u64(bytes, command + 32, align_forward(dwarf_vm_cursor - dwarf_vm_offset, MACH_PAGE_SIZE));
        link_write_u64(bytes, command + 40, dwarf_file_offset);
        link_write_u64(bytes, command + 48, dwarf_file_cursor - dwarf_file_offset);
        link_write_u32(bytes, command + 56, 1);
        link_write_u32(bytes, command + 60, 1);
        link_write_u32(bytes, command + 64, BUSTER_ARRAY_LENGTH(dwarf_kinds));
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(dwarf_kinds); index += 1)
        {
            // Each section must sit inside its segment's address range or
            // dyld rejects the image.
            link_mach_section_write(bytes, command + MACH_SEGMENT_COMMAND_SIZE + (u64)index * MACH_SECTION_SIZE, dwarf_section_names[index], "__DWARF",
                                    image_base + dwarf_vm_offsets[index], object->sections[dwarf_kinds[index]].data.length, (u32)dwarf_file_offsets[index], 0,
                                    0x02000000, 0, 0);
        }
        command += dwarf_command_size;
    }
    BUSTER_LINK_MACH_SEGMENT("__LINKEDIT", image_base + linkedit_vm_offset, align_forward(file_size - linkedit_file_offset, MACH_PAGE_SIZE), linkedit_file_offset,
                             file_size - linkedit_file_offset, 1, 1);
#undef BUSTER_LINK_MACH_SEGMENT
    link_write_u32(bytes, command, 0x80000022);
    link_write_u32(bytes, command + 4, MACH_DYLD_INFO_COMMAND_SIZE);
    link_write_u32(bytes, command + 8, rebase_count ? (u32)linkedit_file_offset : 0);
    link_write_u32(bytes, command + 12, (u32)rebase_size);
    link_write_u32(bytes, command + 16, import_count ? (u32)bind_offset : 0);
    link_write_u32(bytes, command + 20, (u32)bind_size);
    command += MACH_DYLD_INFO_COMMAND_SIZE;
    link_write_u32(bytes, command, 0xe);
    link_write_u32(bytes, command + 4, MACH_DYLINKER_COMMAND_SIZE);
    link_write_u32(bytes, command + 8, 12);
    memcpy(bytes + command + 12, dyld_path, sizeof(dyld_path));
    command += MACH_DYLINKER_COMMAND_SIZE;
    link_write_u32(bytes, command, 0xc);
    link_write_u32(bytes, command + 4, MACH_DYLIB_COMMAND_SIZE);
    link_write_u32(bytes, command + 8, 24);
    link_write_u32(bytes, command + 16, 0x10000);
    link_write_u32(bytes, command + 20, 0x10000);
    memcpy(bytes + command + 24, system_path, sizeof(system_path));
    command += MACH_DYLIB_COMMAND_SIZE;
    for (u32 library_index = 0; library_index < options.dynamic_library_count; library_index += 1)
    {
        String8 library = options.dynamic_libraries[library_index].name;
        u32 dylib_command_size = (u32)align_forward(24 + library.length + 1, 8);
        link_write_u32(bytes, command, 0xc);
        link_write_u32(bytes, command + 4, dylib_command_size);
        link_write_u32(bytes, command + 8, 24);
        link_write_u32(bytes, command + 16, 0x10000);
        link_write_u32(bytes, command + 20, 0x10000);
        memcpy(bytes + command + 24, library.pointer, library.length);
        bytes[command + 24 + library.length] = 0;
        command += dylib_command_size;
    }
    ObjectSymbol* entry_symbol = &object->symbols[entry_symbol_index];
    u64 entry_offset = section_offsets[entry_symbol->section] + entry_symbol->value;
    link_write_u32(bytes, command, 0x80000028);
    link_write_u32(bytes, command + 4, MACH_MAIN_COMMAND_SIZE);
    link_write_u64(bytes, command + 8, entry_offset);
    command += MACH_MAIN_COMMAND_SIZE;
    link_write_u32(bytes, command, 0x32);
    link_write_u32(bytes, command + 4, MACH_BUILD_VERSION_COMMAND_SIZE);
    link_write_u32(bytes, command + 8,
                   object->target.os == OPERATING_SYSTEM_IOS ?
#if BUSTER_IOS_SIMULATOR
                                                             7
                                                             :
#else
                                                             2
                                                             : 1);
#endif
#if BUSTER_IOS_SIMULATOR
                                                             1);
#endif
    link_write_u32(bytes, command + 12, 0x000d0000);
    link_write_u32(bytes, command + 16, 0x000d0000);
    command += MACH_BUILD_VERSION_COMMAND_SIZE;
    link_write_u32(bytes, command, 0x2);
    link_write_u32(bytes, command + 4, MACH_SYMTAB_COMMAND_SIZE);
    link_write_u32(bytes, command + 8, (u32)symbol_table_offset);
    link_write_u32(bytes, command + 12, import_count);
    link_write_u32(bytes, command + 16, (u32)string_table_offset);
    link_write_u32(bytes, command + 20, (u32)string_table_size);
    command += MACH_SYMTAB_COMMAND_SIZE;
    link_write_u32(bytes, command, 0xb);
    link_write_u32(bytes, command + 4, MACH_DYSYMTAB_COMMAND_SIZE);
    link_write_u32(bytes, command + 24, 0);
    link_write_u32(bytes, command + 28, import_count);
    command += MACH_DYSYMTAB_COMMAND_SIZE;
    link_write_u32(bytes, command, 0x1d);
    link_write_u32(bytes, command + 4, MACH_CODE_SIGNATURE_COMMAND_SIZE);
    link_write_u32(bytes, command + 8, (u32)signature_offset);
    link_write_u32(bytes, command + 12, (u32)signature_size);
    command += MACH_CODE_SIGNATURE_COMMAND_SIZE;
    if (command != header_end)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u64 code_directory = signature_offset + 20;
    link_write_u32_be(bytes, signature_offset, 0xfade0cc0);
    link_write_u32_be(bytes, signature_offset + 4, (u32)signature_size);
    link_write_u32_be(bytes, signature_offset + 8, 1);
    link_write_u32_be(bytes, signature_offset + 12, 0);
    link_write_u32_be(bytes, signature_offset + 16, 20);
    link_write_u32_be(bytes, code_directory, 0xfade0c02);
    link_write_u32_be(bytes, code_directory + 4, (u32)code_directory_size);
    link_write_u32_be(bytes, code_directory + 8, 0x20100);
    link_write_u32_be(bytes, code_directory + 12, 2);
    link_write_u32_be(bytes, code_directory + 16, MACH_CODE_DIRECTORY_HEADER_SIZE + MACH_CODE_DIRECTORY_IDENTIFIER_SIZE);
    link_write_u32_be(bytes, code_directory + 20, MACH_CODE_DIRECTORY_HEADER_SIZE);
    link_write_u32_be(bytes, code_directory + 28, (u32)code_slot_count);
    link_write_u32_be(bytes, code_directory + 32, (u32)signature_offset);
    bytes[code_directory + 36] = 32;
    bytes[code_directory + 37] = 2;
    bytes[code_directory + 39] = 12;
    memcpy(bytes + code_directory + MACH_CODE_DIRECTORY_HEADER_SIZE, "buster", MACH_CODE_DIRECTORY_IDENTIFIER_SIZE);
    u64 hash_offset = code_directory + MACH_CODE_DIRECTORY_HEADER_SIZE + MACH_CODE_DIRECTORY_IDENTIFIER_SIZE;
    for (u64 slot = 0; slot < code_slot_count; slot += 1)
    {
        u64 page_offset = slot * MACH_CODE_PAGE_SIZE;
        u64 page_size = BUSTER_MIN((u64)MACH_CODE_PAGE_SIZE, signature_offset - page_offset);
        link_sha256(arena, bytes + page_offset, page_size, bytes + hash_offset + slot * 32);
    }
    if (options.output_path.length && !link_write_executable_file(options.output_path, result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult link_native_executable_android_elf64(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options)
{
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_DYNAMIC_SIZE = 16,
        ELF_DYNAMIC_COUNT = 12,
    };
    static char8 const interpreter[] = "/system/bin/linker64";
    static char8 const library[] = "libc.so";
    NativeExecutableLinkOptions staging_options = options;
    staging_options.output_path = (String8){0};
    ObjectFile staging_object = *object;
    staging_object.target.os = OPERATING_SYSTEM_LINUX;
    bool has_import = options.dynamic_library_count != 0;
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        if (object->symbols[index].section == OBJECT_SECTION_UNDEFINED)
        {
            has_import = true;
            break;
        }
    }
    NativeExecutableLinkResult result = {0};
    if (object->target.cpu_arch == CPU_ARCH_X86_64)
    {
        result = has_import ? link_native_executable_elf64_x86_64_dynamic(arena, &staging_object, staging_options)
                            : link_native_executable_elf64_x86_64(arena, &staging_object, staging_options);
    }
    else if (object->target.cpu_arch == CPU_ARCH_AARCH64)
    {
        result = has_import ? link_native_executable_elf64_aarch64_dynamic(arena, &staging_object, staging_options)
                            : link_native_executable_elf64_aarch64(arena, &staging_object, staging_options);
    }
    else
    {
        result.error = LINK_ERROR_UNSUPPORTED_HOST;
    }
    if (result.error != LINK_ERROR_NONE)
    {
        return result;
    }
    u8* bytes = result.executable.pointer;
    link_write_u16(bytes, 16, 3);
    if (has_import)
    {
        u64 interpreter_program_header = ELF_HEADER_SIZE + ELF_PROGRAM_HEADER_SIZE;
        u64 interpreter_offset = link_read_u64(bytes, interpreter_program_header + 8);
        u64 interpreter_size = link_read_u64(bytes, interpreter_program_header + 32);
        if (sizeof(interpreter) > interpreter_size)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        memset(bytes + interpreter_offset, 0, interpreter_size);
        memcpy(bytes + interpreter_offset, interpreter, sizeof(interpreter));
        u64 dynamic_program_header = ELF_HEADER_SIZE + 4 * ELF_PROGRAM_HEADER_SIZE;
        u64 dynamic_offset = link_read_u64(bytes, dynamic_program_header + 8);
        u64 string_table_offset = 0;
        u32 dynamic_count = ELF_DYNAMIC_COUNT + options.dynamic_library_count;
        for (u32 index = 0; index < dynamic_count; index += 1)
        {
            u64 entry = dynamic_offset + (u64)index * ELF_DYNAMIC_SIZE;
            if (link_read_u64(bytes, entry) == 5)
            {
                string_table_offset = link_read_u64(bytes, entry + 8) - UINT64_C(0x400000);
                break;
            }
        }
        if (!string_table_offset)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        memset(bytes + string_table_offset + 1, 0, sizeof("libc.so.6"));
        memcpy(bytes + string_table_offset + 1, library, sizeof(library));
    }
    if (options.output_path.length && !link_write_executable_file(options.output_path, result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

NativeExecutableLinkResult link_native_executable(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    if (!arena || !object || object->error != OBJECT_ERROR_NONE || object->section_count != OBJECT_SECTION_COUNT || !object->sections ||
        (object->symbol_count && !object->symbols) || (object->relocation_count && !object->relocations) ||
        (options.library_path_count && !options.library_paths) || (options.framework_path_count && !options.framework_paths) ||
        (options.framework_count && !options.frameworks) || (options.linker_argument_count && !options.linker_arguments))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    if (object->target.os == OPERATING_SYSTEM_LINUX && object->target.cpu_arch == CPU_ARCH_X86_64)
    {
        if (options.dynamic_library_count)
        {
            return link_native_executable_elf64_x86_64_dynamic(arena, object, options);
        }
        if (object->section_count > OBJECT_SECTION_THREAD_LOCAL_DATA && object->sections &&
            (object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length || object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size))
        {
            return link_native_executable_elf64_x86_64_dynamic(arena, object, options);
        }
        for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
        {
            if (object->symbols[symbol_index].section == OBJECT_SECTION_UNDEFINED)
            {
                return link_native_executable_elf64_x86_64_dynamic(arena, object, options);
            }
        }
        return link_native_executable_elf64_x86_64(arena, object, options);
    }
    if (object->target.os == OPERATING_SYSTEM_WINDOWS && (object->target.cpu_arch == CPU_ARCH_X86_64 || object->target.cpu_arch == CPU_ARCH_AARCH64))
    {
        return link_native_executable_pe64(arena, object, options);
    }
    if ((object->target.os == OPERATING_SYSTEM_MACOS || object->target.os == OPERATING_SYSTEM_IOS) &&
        (object->target.cpu_arch == CPU_ARCH_X86_64 || object->target.cpu_arch == CPU_ARCH_AARCH64))
    {
        return link_native_executable_mach_o64(arena, object, options);
    }
    if (object->target.os == OPERATING_SYSTEM_ANDROID && (object->target.cpu_arch == CPU_ARCH_X86_64 || object->target.cpu_arch == CPU_ARCH_AARCH64))
    {
        return link_native_executable_android_elf64(arena, object, options);
    }
    if (object->target.os == OPERATING_SYSTEM_LINUX && object->target.cpu_arch == CPU_ARCH_AARCH64)
    {
        if (options.dynamic_library_count)
        {
            return link_native_executable_elf64_aarch64_dynamic(arena, object, options);
        }
        if (object->section_count > OBJECT_SECTION_THREAD_LOCAL_DATA && object->sections &&
            (object->sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length || object->sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size))
        {
            return link_native_executable_elf64_aarch64_dynamic(arena, object, options);
        }
        for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
        {
            if (object->symbols[symbol_index].section == OBJECT_SECTION_UNDEFINED)
            {
                return link_native_executable_elf64_aarch64_dynamic(arena, object, options);
            }
        }
        return link_native_executable_elf64_aarch64(arena, object, options);
    }
    result.error = LINK_ERROR_UNSUPPORTED_HOST;
    return result;
}
