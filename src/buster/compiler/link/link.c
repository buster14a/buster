#include <buster/compiler/link/link.h>

#include <buster/file.h>
#include <buster/integer.h>
#include <buster/os.h>
#include <buster/string.h>

BUSTER_GLOBAL_LOCAL String8 link_string_copy(
    Arena* arena,
    String8 source)
{
    String8 result = {0};
    if (source.length)
    {
        result.pointer = arena_allocate(
            arena,
            char8,
            source.length);
        result.length = source.length;
        memcpy(
            result.pointer,
            source.pointer,
            source.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool link_target_matches(
    Target left,
    Target right)
{
    return
        left.cpu_arch == right.cpu_arch &&
        left.os == right.os;
}

BUSTER_GLOBAL_LOCAL u32 link_global_symbol_find(
    ObjectSymbol* symbols,
    u32 symbol_count,
    String8 name)
{
    for (u32 index = 0;
        index < symbol_count;
        index += 1)
    {
        if (symbols[index].global &&
            string_equal(symbols[index].name, name))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool link_symbol_definition_set(
    ObjectSymbol* destination,
    ObjectSymbol* source,
    ObjectFile* object,
    u64* section_offsets,
    Arena* arena)
{
    *destination = *source;
    destination->name =
        link_string_copy(arena, source->name);
    if (source->section == OBJECT_SECTION_UNDEFINED)
    {
        return true;
    }
    if (source->section >= object->section_count)
    {
        return false;
    }
    ObjectSectionKind kind =
        object->sections[source->section].kind;
    if (kind >= OBJECT_SECTION_COUNT)
    {
        return false;
    }
    destination->section = (u32)kind;
    destination->value +=
        section_offsets[source->section];
    return true;
}

LinkObjectResult link_objects(
    Arena* arena,
    ObjectFile* objects,
    u32 object_count,
    LinkOptions options)
{
    LinkObjectResult result = {0};
    if (!arena || !objects || !object_count)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    Target target = objects[0].target;
    u64 section_sizes[OBJECT_SECTION_COUNT] = {0};
    u32 section_alignments[OBJECT_SECTION_COUNT] = {
        [OBJECT_SECTION_TEXT] = 1,
        [OBJECT_SECTION_READ_ONLY_DATA] = 1,
        [OBJECT_SECTION_DATA] = 1,
    };
    u64 total_symbols = 0;
    u64 total_relocations = 0;
    u64 offset_count =
        (u64)object_count * OBJECT_SECTION_COUNT;
    u64* section_offsets = arena_allocate(
        arena,
        u64,
        offset_count);
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        if (object->error != OBJECT_ERROR_NONE ||
            !object->sections ||
            object->section_count >
                OBJECT_SECTION_COUNT ||
            (object->symbol_count && !object->symbols) ||
            (object->relocation_count &&
                !object->relocations))
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
        if (total_symbols > UINT32_MAX ||
            total_relocations > UINT32_MAX)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        for (u32 section_index = 0;
            section_index < object->section_count;
            section_index += 1)
        {
            ObjectSection* section =
                &object->sections[section_index];
            if (section->kind >=
                    OBJECT_SECTION_COUNT ||
                !section->alignment ||
                !BUSTER_IS_POWER_OF_TWO(
                    section->alignment) ||
                (section->data.length &&
                    !section->data.pointer))
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            u32 alignment = section->alignment;
            u64 aligned = align_forward(
                section_sizes[section->kind],
                alignment);
            if (aligned <
                    section_sizes[section->kind] ||
                section->data.length >
                    UINT64_MAX - aligned)
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            section_offsets[
                (u64)object_index *
                    OBJECT_SECTION_COUNT +
                section_index] = aligned;
            section_sizes[section->kind] =
                aligned + section->data.length;
            section_alignments[section->kind] =
                BUSTER_MAX(
                    section_alignments[section->kind],
                    alignment);
        }
    }
    result.object = (ObjectFile){
        .sections = arena_allocate(
            arena,
            ObjectSection,
            OBJECT_SECTION_COUNT),
        .symbols = arena_allocate(
            arena,
            ObjectSymbol,
            total_symbols),
        .relocations = arena_allocate(
            arena,
            ObjectRelocation,
            total_relocations),
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
    };
    String8 section_names[OBJECT_SECTION_COUNT] = {
        [OBJECT_SECTION_TEXT] = S8(".text"),
        [OBJECT_SECTION_READ_ONLY_DATA] =
            S8(".rodata"),
        [OBJECT_SECTION_DATA] = S8(".data"),
    };
    for (u32 kind = 0;
        kind < OBJECT_SECTION_COUNT;
        kind += 1)
    {
        u8* data = arena_allocate(
            arena,
            u8,
            section_sizes[kind]);
        result.object.sections[kind] =
            (ObjectSection){
                .name = section_names[kind],
                .data = {
                    .pointer = data,
                    .length = section_sizes[kind],
                },
                .kind = (ObjectSectionKind)kind,
                .alignment =
                    section_alignments[kind],
            };
    }
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u64* offsets =
            section_offsets +
            (u64)object_index *
                OBJECT_SECTION_COUNT;
        for (u32 section_index = 0;
            section_index < object->section_count;
            section_index += 1)
        {
            ObjectSection* source =
                &object->sections[section_index];
            if (source->data.length)
            {
                memcpy(
                    result.object.sections[
                        source->kind].data.pointer +
                        offsets[section_index],
                    source->data.pointer,
                    source->data.length);
            }
        }
    }
    u32** symbol_maps = arena_allocate(
        arena,
        u32*,
        object_count);
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u32* symbol_map = arena_allocate(
            arena,
            u32,
            object->symbol_count);
        symbol_maps[object_index] = symbol_map;
        for (u32 source_index = 0;
            source_index < object->symbol_count;
            source_index += 1)
        {
            symbol_map[source_index] = UINT32_MAX;
            ObjectSymbol* source =
                &object->symbols[source_index];
            if (!source->name.length)
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            if (source->section !=
                    OBJECT_SECTION_UNDEFINED &&
                (source->section >=
                        object->section_count ||
                    source->value >
                        object->sections[
                            source->section]
                            .data.length ||
                    source->size >
                        object->sections[
                            source->section]
                            .data.length -
                            source->value))
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            u32 destination_index = UINT32_MAX;
            if (source->global)
            {
                destination_index =
                    link_global_symbol_find(
                        result.object.symbols,
                        result.object.symbol_count,
                        source->name);
            }
            if (destination_index == UINT32_MAX)
            {
                destination_index =
                    result.object.symbol_count++;
                if (!link_symbol_definition_set(
                        &result.object.symbols[
                            destination_index],
                        source,
                        object,
                        section_offsets +
                            (u64)object_index *
                                OBJECT_SECTION_COUNT,
                        arena))
                {
                    result.error =
                        LINK_ERROR_INVALID_INPUT;
                    return result;
                }
            }
            else
            {
                ObjectSymbol* destination =
                    &result.object.symbols[
                        destination_index];
                bool destination_defined =
                    destination->section !=
                        OBJECT_SECTION_UNDEFINED;
                bool source_defined =
                    source->section !=
                        OBJECT_SECTION_UNDEFINED;
                if (destination_defined &&
                    source_defined)
                {
                    result.error =
                        LINK_ERROR_DUPLICATE_SYMBOL;
                    result.symbol =
                        link_string_copy(
                            arena,
                            source->name);
                    return result;
                }
                if (!destination_defined &&
                    source_defined)
                {
                    if (!link_symbol_definition_set(
                            destination,
                            source,
                            object,
                            section_offsets +
                                (u64)object_index *
                                    OBJECT_SECTION_COUNT,
                            arena))
                    {
                        result.error =
                            LINK_ERROR_INVALID_INPUT;
                        return result;
                    }
                }
            }
            symbol_map[source_index] =
                destination_index;
        }
    }
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u64* offsets =
            section_offsets +
            (u64)object_index *
                OBJECT_SECTION_COUNT;
        for (u32 relocation_index = 0;
            relocation_index <
                object->relocation_count;
            relocation_index += 1)
        {
            ObjectRelocation source =
                object->relocations[relocation_index];
            if (source.section >=
                    object->section_count ||
                source.symbol >=
                    object->symbol_count ||
                symbol_maps[object_index][
                    source.symbol] == UINT32_MAX)
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            ObjectSectionKind kind =
                object->sections[
                    source.section].kind;
            source.section = (u32)kind;
            source.offset +=
                offsets[
                    object->relocations[
                        relocation_index].section];
            source.symbol =
                symbol_maps[object_index][
                    source.symbol];
            result.object.relocations[
                result.object.relocation_count++] =
                    source;
        }
    }
    if (!options.allow_undefined_symbols)
    {
        for (u32 symbol_index = 0;
            symbol_index <
                result.object.symbol_count;
            symbol_index += 1)
        {
            ObjectSymbol* symbol =
                &result.object.symbols[symbol_index];
            if (symbol->section ==
                OBJECT_SECTION_UNDEFINED)
            {
                result.error =
                    LINK_ERROR_UNRESOLVED_SYMBOL;
                result.symbol =
                    link_string_copy(
                        arena,
                        symbol->name);
                return result;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void link_write_u16(
    u8* bytes,
    u64 offset,
    u16 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void link_write_u32(
    u8* bytes,
    u64 offset,
    u32 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL void link_write_u64(
    u8* bytes,
    u64 offset,
    u64 value)
{
    memcpy(bytes + offset, &value, sizeof(value));
}

BUSTER_GLOBAL_LOCAL u64 link_read_u64(
    u8 const* bytes,
    u64 offset)
{
    u64 result = 0;
    memcpy(&result, bytes + offset, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL void link_write_u32_be(
    u8* bytes,
    u64 offset,
    u32 value)
{
    bytes[offset] = (u8)(value >> 24);
    bytes[offset + 1] = (u8)(value >> 16);
    bytes[offset + 2] = (u8)(value >> 8);
    bytes[offset + 3] = (u8)value;
}

BUSTER_GLOBAL_LOCAL u32 link_rotate_right_u32(
    u32 value,
    u32 amount)
{
    return (value >> amount) |
        (value << (32 - amount));
}

BUSTER_GLOBAL_LOCAL void link_sha256(
    u8 const* input,
    u64 length,
    u8 output[32])
{
    static u32 const constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    u32 state[8] = {
        0x6a09e667,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19,
    };
    u64 block_count = (length + 9 + 63) / 64;
    for (u64 block_index = 0;
        block_index < block_count;
        block_index += 1)
    {
        u8 block[64] = {0};
        u64 block_offset = block_index * 64;
        for (u64 byte_index = 0;
            byte_index < 64;
            byte_index += 1)
        {
            u64 source_offset =
                block_offset + byte_index;
            if (source_offset < length)
            {
                block[byte_index] =
                    input[source_offset];
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
                block[63 - index] =
                    (u8)(bit_length >> (index * 8));
            }
        }
        u32 words[64] = {0};
        for (u32 index = 0; index < 16; index += 1)
        {
            u32 offset = index * 4;
            words[index] =
                ((u32)block[offset] << 24) |
                ((u32)block[offset + 1] << 16) |
                ((u32)block[offset + 2] << 8) |
                block[offset + 3];
        }
        for (u32 index = 16; index < 64; index += 1)
        {
            u32 first =
                link_rotate_right_u32(
                    words[index - 15],
                    7) ^
                link_rotate_right_u32(
                    words[index - 15],
                    18) ^
                (words[index - 15] >> 3);
            u32 second =
                link_rotate_right_u32(
                    words[index - 2],
                    17) ^
                link_rotate_right_u32(
                    words[index - 2],
                    19) ^
                (words[index - 2] >> 10);
            words[index] =
                words[index - 16] +
                first +
                words[index - 7] +
                second;
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
            u32 sigma1 =
                link_rotate_right_u32(e, 6) ^
                link_rotate_right_u32(e, 11) ^
                link_rotate_right_u32(e, 25);
            u32 choice = (e & f) ^ (~e & g);
            u32 temporary1 =
                h + sigma1 + choice +
                constants[index] + words[index];
            u32 sigma0 =
                link_rotate_right_u32(a, 2) ^
                link_rotate_right_u32(a, 13) ^
                link_rotate_right_u32(a, 22);
            u32 majority =
                (a & b) ^ (a & c) ^ (b & c);
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
        link_write_u32_be(
            output,
            (u64)index * 4,
            state[index]);
    }
}

BUSTER_GLOBAL_LOCAL bool link_write_executable_file(
    String8 path,
    ByteSlice bytes)
{
    OsFileDescriptor* file = os_file_open(
        path,
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

BUSTER_GLOBAL_LOCAL u32 link_symbol_find(
    ObjectFile* object,
    String8 name)
{
    for (u32 index = 0;
        index < object->symbol_count;
        index += 1)
    {
        if (string_equal(
                object->symbols[index].name,
                name))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult
link_native_executable_elf64_x86_64(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
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
        0x31, 0xed,
        0x48, 0x8b, 0x3c, 0x24,
        0x48, 0x8d, 0x74, 0x24, 0x08,
        0x48, 0x8d, 0x54, 0xfe, 0x08,
        0x48, 0x83, 0xe4, 0xf0,
        0xe8, 0, 0, 0, 0,
        0x89, 0xc7,
        0xb8, 0x3c, 0, 0, 0,
        0x0f, 0x05,
        0xf4,
    };
    if (object->section_count <
            OBJECT_SECTION_COUNT ||
        !object->sections ||
        (object->symbol_count && !object->symbols) ||
        (object->relocation_count &&
            !object->relocations))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        if (object->symbols[symbol_index].section ==
            OBJECT_SECTION_UNDEFINED)
        {
            result.error =
                LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol =
                object->symbols[symbol_index].name;
            return result;
        }
    }
    String8 entry_name = options.entry_symbol.length ?
        options.entry_symbol :
        S8("main");
    u32 entry_symbol_index =
        link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX ||
        object->symbols[entry_symbol_index].section ==
            OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind !=
            OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    u32 program_header_count =
        object->sections[OBJECT_SECTION_DATA]
            .data.length ? 2 : 1;
    u64 header_end =
        ELF_HEADER_SIZE +
        (u64)program_header_count *
            ELF_PROGRAM_HEADER_SIZE;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 entry_stub_offset =
        align_forward(header_end, 16);
    section_offsets[OBJECT_SECTION_TEXT] =
        align_forward(
            entry_stub_offset +
                sizeof(entry_stub),
            object->sections[
                OBJECT_SECTION_TEXT].alignment);
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] =
        align_forward(
            section_offsets[OBJECT_SECTION_TEXT] +
                object->sections[
                    OBJECT_SECTION_TEXT].data.length,
            object->sections[
                OBJECT_SECTION_READ_ONLY_DATA]
                .alignment);
    u64 read_only_end =
        section_offsets[
            OBJECT_SECTION_READ_ONLY_DATA] +
        object->sections[
            OBJECT_SECTION_READ_ONLY_DATA]
            .data.length;
    section_offsets[OBJECT_SECTION_DATA] =
        align_forward(
            read_only_end,
            ELF_PAGE_SIZE);
    u64 file_size =
        object->sections[OBJECT_SECTION_DATA]
            .data.length ?
            section_offsets[OBJECT_SECTION_DATA] +
                object->sections[
                    OBJECT_SECTION_DATA].data.length :
            read_only_end;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(
            arena,
            u8,
            file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(bytes + entry_stub_offset,
        entry_stub,
        sizeof(entry_stub));
    for (u32 section = 0;
        section < OBJECT_SECTION_COUNT;
        section += 1)
    {
        ByteSlice data =
            object->sections[section].data;
        if (data.length)
        {
            memcpy(
                bytes + section_offsets[section],
                data.pointer,
                data.length);
        }
    }
    u64 image_base = 0x400000;
    ObjectSymbol* entry_symbol =
        &object->symbols[entry_symbol_index];
    u64 entry_address =
        image_base +
        section_offsets[entry_symbol->section] +
        entry_symbol->value;
    u64 call_displacement_offset =
        entry_stub_offset + 21;
    s64 call_displacement =
        (s64)entry_address -
        (s64)(image_base +
            call_displacement_offset + 4);
    if (call_displacement < INT32_MIN ||
        call_displacement > INT32_MAX)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(
        bytes,
        call_displacement_offset,
        (u32)(s32)call_displacement);
    for (u32 index = 0;
        index < object->relocation_count;
        index += 1)
    {
        ObjectRelocation* relocation =
            &object->relocations[index];
        if (relocation->section >=
                OBJECT_SECTION_COUNT ||
            relocation->symbol >=
                object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section =
            &object->sections[relocation->section];
        u64 width =
            relocation->kind ==
                OBJECT_RELOCATION_ABSOLUTE64 ?
                8 : 4;
        if (relocation->offset >
                section->data.length ||
            width >
                section->data.length -
                    relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol =
            &object->symbols[relocation->symbol];
        if (symbol->section >=
            OBJECT_SECTION_COUNT)
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 symbol_address =
            image_base +
            section_offsets[symbol->section] +
            symbol->value;
        u64 place_address =
            image_base +
            section_offsets[relocation->section] +
            relocation->offset;
        u64 output_offset =
            section_offsets[relocation->section] +
            relocation->offset;
        if (relocation->kind ==
            OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 value =
                (s64)symbol_address +
                relocation->addend -
                (s64)place_address;
            if (value < INT32_MIN ||
                value > INT32_MAX)
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                output_offset,
                (u32)(s32)value);
        }
        else if (relocation->kind ==
            OBJECT_RELOCATION_ABSOLUTE64)
        {
            link_write_u64(
                bytes,
                output_offset,
                symbol_address +
                    (u64)relocation->addend);
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    link_write_u16(bytes, 16, 2);
    link_write_u16(
        bytes,
        18,
        ELF_MACHINE_X86_64);
    link_write_u32(bytes, 20, 1);
    link_write_u64(
        bytes,
        24,
        image_base + entry_stub_offset);
    link_write_u64(bytes, 32, ELF_HEADER_SIZE);
    link_write_u16(bytes, 52, ELF_HEADER_SIZE);
    link_write_u16(
        bytes,
        54,
        ELF_PROGRAM_HEADER_SIZE);
    link_write_u16(
        bytes,
        56,
        (u16)program_header_count);
    u64 program_header = ELF_HEADER_SIZE;
    link_write_u32(bytes, program_header, 1);
    link_write_u32(bytes, program_header + 4, 5);
    link_write_u64(
        bytes,
        program_header + 16,
        image_base);
    link_write_u64(
        bytes,
        program_header + 24,
        image_base);
    link_write_u64(
        bytes,
        program_header + 32,
        read_only_end);
    link_write_u64(
        bytes,
        program_header + 40,
        read_only_end);
    link_write_u64(
        bytes,
        program_header + 48,
        ELF_PAGE_SIZE);
    if (program_header_count == 2)
    {
        program_header +=
            ELF_PROGRAM_HEADER_SIZE;
        u64 data_size =
            object->sections[
                OBJECT_SECTION_DATA].data.length;
        link_write_u32(bytes, program_header, 1);
        link_write_u32(
            bytes,
            program_header + 4,
            6);
        link_write_u64(
            bytes,
            program_header + 8,
            section_offsets[
                OBJECT_SECTION_DATA]);
        link_write_u64(
            bytes,
            program_header + 16,
            image_base +
                section_offsets[
                    OBJECT_SECTION_DATA]);
        link_write_u64(
            bytes,
            program_header + 24,
            image_base +
                section_offsets[
                    OBJECT_SECTION_DATA]);
        link_write_u64(
            bytes,
            program_header + 32,
            data_size);
        link_write_u64(
            bytes,
            program_header + 40,
            data_size);
        link_write_u64(
            bytes,
            program_header + 48,
            ELF_PAGE_SIZE);
    }
    if (options.output_path.length &&
        !link_write_executable_file(
            options.output_path,
            result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult
link_native_executable_elf64_x86_64_dynamic(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_PROGRAM_HEADER_COUNT = 6,
        ELF_PAGE_SIZE = 4096,
        ELF_MACHINE_X86_64 = 62,
        ELF_SYMBOL_SIZE = 24,
        ELF_RELOCATION_SIZE = 24,
        ELF_DYNAMIC_SIZE = 16,
        ELF_DYNAMIC_COUNT = 12,
        ELF_PLT_ENTRY_SIZE = 16,
        ELF_GOT_RESERVED_COUNT = 3,
    };
    static u8 const entry_stub[] = {
        0x31, 0xed,
        0x48, 0x8b, 0x3c, 0x24,
        0x48, 0x8d, 0x74, 0x24, 0x08,
        0x48, 0x8d, 0x54, 0xfe, 0x08,
        0x48, 0x83, 0xe4, 0xf0,
        0xe8, 0, 0, 0, 0,
        0x89, 0xc7,
        0xb8, 0x3c, 0, 0, 0,
        0x0f, 0x05,
        0xf4,
    };
    static char8 const interpreter[] =
        "/lib64/ld-linux-x86-64.so.2";
    static char8 const library_name[] = "libc.so.6";
    if (object->section_count <
            OBJECT_SECTION_COUNT ||
        !object->sections ||
        (object->symbol_count && !object->symbols) ||
        (object->relocation_count &&
            !object->relocations))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u32 import_count = 0;
    u64 imported_name_size = 0;
    u32* import_indices = arena_allocate(
        arena,
        u32,
        object->symbol_count);
    u32* import_name_offsets = arena_allocate(
        arena,
        u32,
        object->symbol_count);
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        ObjectSymbol* symbol =
            &object->symbols[symbol_index];
        if (symbol->section !=
            OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        if (!symbol->global ||
            symbol->kind != OBJECT_SYMBOL_FUNCTION ||
            !symbol->name.length ||
            symbol->name.length > UINT32_MAX ||
            imported_name_size >
                UINT32_MAX - symbol->name.length - 1)
        {
            result.error =
                LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = symbol->name;
            return result;
        }
        import_indices[symbol_index] = import_count;
        imported_name_size +=
            symbol->name.length + 1;
        import_count += 1;
    }
    if (!import_count)
    {
        return link_native_executable_elf64_x86_64(
            arena,
            object,
            options);
    }
    String8 entry_name = options.entry_symbol.length ?
        options.entry_symbol :
        S8("main");
    u32 entry_symbol_index =
        link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX ||
        object->symbols[entry_symbol_index].section ==
            OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind !=
            OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    u64 header_end =
        ELF_HEADER_SIZE +
        ELF_PROGRAM_HEADER_COUNT *
            ELF_PROGRAM_HEADER_SIZE;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 entry_stub_offset =
        align_forward(header_end, 16);
    section_offsets[OBJECT_SECTION_TEXT] =
        align_forward(
            entry_stub_offset +
                sizeof(entry_stub),
            object->sections[
                OBJECT_SECTION_TEXT].alignment);
    u64 plt_offset = align_forward(
        section_offsets[OBJECT_SECTION_TEXT] +
            object->sections[
                OBJECT_SECTION_TEXT].data.length,
        16);
    u64 plt_size =
        (u64)(import_count + 1) *
        ELF_PLT_ENTRY_SIZE;
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] =
        align_forward(
            plt_offset + plt_size,
            object->sections[
                OBJECT_SECTION_READ_ONLY_DATA]
                .alignment);
    u64 interpreter_offset =
        section_offsets[
            OBJECT_SECTION_READ_ONLY_DATA] +
        object->sections[
            OBJECT_SECTION_READ_ONLY_DATA]
            .data.length;
    u64 interpreter_size =
        sizeof(interpreter);
    u64 dynamic_string_offset =
        interpreter_offset + interpreter_size;
    u32 library_name_offset = 1;
    u32 first_symbol_name_offset =
        library_name_offset +
        (u32)sizeof(library_name);
    u64 dynamic_string_size =
        1 + sizeof(library_name) +
        imported_name_size;
    u64 dynamic_symbol_offset = align_forward(
        dynamic_string_offset +
            dynamic_string_size,
        8);
    u64 dynamic_symbol_size =
        (u64)(import_count + 1) *
        ELF_SYMBOL_SIZE;
    u64 hash_offset = align_forward(
        dynamic_symbol_offset +
            dynamic_symbol_size,
        4);
    u64 hash_size =
        (u64)(2 + 1 + import_count + 1) *
        sizeof(u32);
    u64 relocation_offset = align_forward(
        hash_offset + hash_size,
        8);
    u64 relocation_size =
        (u64)import_count *
        ELF_RELOCATION_SIZE;
    u64 read_only_end =
        relocation_offset + relocation_size;
    section_offsets[OBJECT_SECTION_DATA] =
        align_forward(read_only_end, ELF_PAGE_SIZE);
    u64 got_offset = align_forward(
        section_offsets[OBJECT_SECTION_DATA] +
            object->sections[
                OBJECT_SECTION_DATA].data.length,
        8);
    u64 got_size =
        (u64)(ELF_GOT_RESERVED_COUNT +
            import_count) * sizeof(u64);
    u64 dynamic_offset = align_forward(
        got_offset + got_size,
        8);
    u64 dynamic_size =
        ELF_DYNAMIC_COUNT * ELF_DYNAMIC_SIZE;
    u64 file_size =
        dynamic_offset + dynamic_size;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(
            arena,
            u8,
            file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(bytes + entry_stub_offset,
        entry_stub,
        sizeof(entry_stub));
    for (u32 section = 0;
        section < OBJECT_SECTION_COUNT;
        section += 1)
    {
        ByteSlice data =
            object->sections[section].data;
        if (data.length)
        {
            memcpy(
                bytes + section_offsets[section],
                data.pointer,
                data.length);
        }
    }
    memcpy(
        bytes + interpreter_offset,
        interpreter,
        interpreter_size);
    memcpy(
        bytes + dynamic_string_offset +
            library_name_offset,
        library_name,
        sizeof(library_name));
    u64 dynamic_name_cursor =
        dynamic_string_offset +
        first_symbol_name_offset;
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        u32 import_index =
            import_indices[symbol_index];
        if (import_index == UINT32_MAX)
        {
            continue;
        }
        ObjectSymbol* symbol =
            &object->symbols[symbol_index];
        import_name_offsets[import_index] =
            (u32)(dynamic_name_cursor -
                dynamic_string_offset);
        memcpy(
            bytes + dynamic_name_cursor,
            symbol->name.pointer,
            symbol->name.length);
        dynamic_name_cursor +=
            symbol->name.length + 1;
        u64 symbol_offset =
            dynamic_symbol_offset +
            (u64)(import_index + 1) *
                ELF_SYMBOL_SIZE;
        link_write_u32(
            bytes,
            symbol_offset,
            import_name_offsets[import_index]);
        bytes[symbol_offset + 4] = 0x12;
    }
    link_write_u32(bytes, hash_offset, 1);
    link_write_u32(
        bytes,
        hash_offset + 4,
        import_count + 1);
    link_write_u32(
        bytes,
        hash_offset + 8,
        1);
    for (u32 import_index = 1;
        import_index <= import_count;
        import_index += 1)
    {
        link_write_u32(
            bytes,
            hash_offset +
                (u64)(3 + import_index) *
                    sizeof(u32),
            import_index == import_count ?
                0 : import_index + 1);
    }
    u64 image_base = 0x400000;
    u64 got_address = image_base + got_offset;
    u64 plt_address = image_base + plt_offset;
    u64 dynamic_address =
        image_base + dynamic_offset;
    bytes[plt_offset] = 0xff;
    bytes[plt_offset + 1] = 0x35;
    link_write_u32(
        bytes,
        plt_offset + 2,
        (u32)(s32)(
            (s64)(got_address + 8) -
            (s64)(plt_address + 6)));
    bytes[plt_offset + 6] = 0xff;
    bytes[plt_offset + 7] = 0x25;
    link_write_u32(
        bytes,
        plt_offset + 8,
        (u32)(s32)(
            (s64)(got_address + 16) -
            (s64)(plt_address + 12)));
    bytes[plt_offset + 12] = 0x0f;
    bytes[plt_offset + 13] = 0x1f;
    bytes[plt_offset + 14] = 0x40;
    link_write_u64(bytes, got_offset, dynamic_address);
    for (u32 import_index = 0;
        import_index < import_count;
        import_index += 1)
    {
        u64 entry_offset =
            plt_offset +
            (u64)(import_index + 1) *
                ELF_PLT_ENTRY_SIZE;
        u64 entry_address =
            image_base + entry_offset;
        u64 slot_offset =
            got_offset +
            (u64)(ELF_GOT_RESERVED_COUNT +
                import_index) * sizeof(u64);
        u64 slot_address =
            image_base + slot_offset;
        bytes[entry_offset] = 0xff;
        bytes[entry_offset + 1] = 0x25;
        link_write_u32(
            bytes,
            entry_offset + 2,
            (u32)(s32)(
                (s64)slot_address -
                (s64)(entry_address + 6)));
        bytes[entry_offset + 6] = 0x68;
        link_write_u32(
            bytes,
            entry_offset + 7,
            import_index);
        bytes[entry_offset + 11] = 0xe9;
        link_write_u32(
            bytes,
            entry_offset + 12,
            (u32)(s32)(
                (s64)plt_address -
                (s64)(entry_address + 16)));
        link_write_u64(
            bytes,
            slot_offset,
            entry_address + 6);
        u64 relocation_entry =
            relocation_offset +
            (u64)import_index *
                ELF_RELOCATION_SIZE;
        link_write_u64(
            bytes,
            relocation_entry,
            slot_address);
        link_write_u64(
            bytes,
            relocation_entry + 8,
            ((u64)(import_index + 1) << 32) |
                7);
    }
    ObjectSymbol* entry_symbol =
        &object->symbols[entry_symbol_index];
    u64 entry_address =
        image_base +
        section_offsets[entry_symbol->section] +
        entry_symbol->value;
    u64 call_displacement_offset =
        entry_stub_offset + 21;
    s64 call_displacement =
        (s64)entry_address -
        (s64)(image_base +
            call_displacement_offset + 4);
    if (call_displacement < INT32_MIN ||
        call_displacement > INT32_MAX)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(
        bytes,
        call_displacement_offset,
        (u32)(s32)call_displacement);
    for (u32 index = 0;
        index < object->relocation_count;
        index += 1)
    {
        ObjectRelocation* relocation =
            &object->relocations[index];
        if (relocation->section >=
                OBJECT_SECTION_COUNT ||
            relocation->symbol >=
                object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section =
            &object->sections[relocation->section];
        u64 width =
            relocation->kind ==
                OBJECT_RELOCATION_ABSOLUTE64 ?
                8 : 4;
        if (relocation->offset >
                section->data.length ||
            width >
                section->data.length -
                    relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol =
            &object->symbols[relocation->symbol];
        u64 symbol_address = 0;
        if (symbol->section ==
            OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index =
                import_indices[relocation->symbol];
            if (import_index == UINT32_MAX ||
                relocation->kind !=
                    OBJECT_RELOCATION_X86_64_PC32)
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_address =
                plt_address +
                (u64)(import_index + 1) *
                    ELF_PLT_ENTRY_SIZE;
        }
        else if (symbol->section <
            OBJECT_SECTION_COUNT)
        {
            symbol_address =
                image_base +
                section_offsets[symbol->section] +
                symbol->value;
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 place_address =
            image_base +
            section_offsets[relocation->section] +
            relocation->offset;
        u64 output_offset =
            section_offsets[relocation->section] +
            relocation->offset;
        if (relocation->kind ==
            OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 value =
                (s64)symbol_address +
                relocation->addend -
                (s64)place_address;
            if (value < INT32_MIN ||
                value > INT32_MAX)
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                output_offset,
                (u32)(s32)value);
        }
        else if (relocation->kind ==
            OBJECT_RELOCATION_ABSOLUTE64)
        {
            link_write_u64(
                bytes,
                output_offset,
                symbol_address +
                    (u64)relocation->addend);
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    u64 dynamic_cursor = dynamic_offset;
#define BUSTER_LINK_DYNAMIC(tag, value) \
    do \
    { \
        link_write_u64(bytes, dynamic_cursor, (tag)); \
        link_write_u64(bytes, dynamic_cursor + 8, (value)); \
        dynamic_cursor += ELF_DYNAMIC_SIZE; \
    } while (0)
    BUSTER_LINK_DYNAMIC(1, library_name_offset);
    BUSTER_LINK_DYNAMIC(
        4,
        image_base + hash_offset);
    BUSTER_LINK_DYNAMIC(
        5,
        image_base + dynamic_string_offset);
    BUSTER_LINK_DYNAMIC(
        6,
        image_base + dynamic_symbol_offset);
    BUSTER_LINK_DYNAMIC(10, dynamic_string_size);
    BUSTER_LINK_DYNAMIC(11, ELF_SYMBOL_SIZE);
    BUSTER_LINK_DYNAMIC(3, got_address);
    BUSTER_LINK_DYNAMIC(2, relocation_size);
    BUSTER_LINK_DYNAMIC(20, 7);
    BUSTER_LINK_DYNAMIC(
        23,
        image_base + relocation_offset);
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
    link_write_u16(
        bytes,
        18,
        ELF_MACHINE_X86_64);
    link_write_u32(bytes, 20, 1);
    link_write_u64(
        bytes,
        24,
        image_base + entry_stub_offset);
    link_write_u64(bytes, 32, ELF_HEADER_SIZE);
    link_write_u16(bytes, 52, ELF_HEADER_SIZE);
    link_write_u16(
        bytes,
        54,
        ELF_PROGRAM_HEADER_SIZE);
    link_write_u16(
        bytes,
        56,
        ELF_PROGRAM_HEADER_COUNT);
    u64 program_header = ELF_HEADER_SIZE;
#define BUSTER_LINK_PROGRAM_HEADER( \
        type, flags, offset, address, size, alignment) \
    do \
    { \
        link_write_u32(bytes, program_header, (type)); \
        link_write_u32(bytes, program_header + 4, (flags)); \
        link_write_u64(bytes, program_header + 8, (offset)); \
        link_write_u64(bytes, program_header + 16, (address)); \
        link_write_u64(bytes, program_header + 24, (address)); \
        link_write_u64(bytes, program_header + 32, (size)); \
        link_write_u64(bytes, program_header + 40, (size)); \
        link_write_u64(bytes, program_header + 48, (alignment)); \
        program_header += ELF_PROGRAM_HEADER_SIZE; \
    } while (0)
    BUSTER_LINK_PROGRAM_HEADER(
        6,
        4,
        ELF_HEADER_SIZE,
        image_base + ELF_HEADER_SIZE,
        ELF_PROGRAM_HEADER_COUNT *
            ELF_PROGRAM_HEADER_SIZE,
        8);
    BUSTER_LINK_PROGRAM_HEADER(
        3,
        4,
        interpreter_offset,
        image_base + interpreter_offset,
        interpreter_size,
        1);
    BUSTER_LINK_PROGRAM_HEADER(
        1,
        5,
        0,
        image_base,
        read_only_end,
        ELF_PAGE_SIZE);
    BUSTER_LINK_PROGRAM_HEADER(
        1,
        6,
        section_offsets[OBJECT_SECTION_DATA],
        image_base +
            section_offsets[OBJECT_SECTION_DATA],
        file_size -
            section_offsets[OBJECT_SECTION_DATA],
        ELF_PAGE_SIZE);
    BUSTER_LINK_PROGRAM_HEADER(
        2,
        6,
        dynamic_offset,
        dynamic_address,
        dynamic_size,
        8);
    BUSTER_LINK_PROGRAM_HEADER(
        0x6474e551,
        6,
        0,
        0,
        0,
        16);
#undef BUSTER_LINK_PROGRAM_HEADER
    if (options.output_path.length &&
        !link_write_executable_file(
            options.output_path,
            result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void link_pe_section_header(
    u8* bytes,
    u64 offset,
    char const name[8],
    u32 virtual_size,
    u32 virtual_address,
    u32 raw_size,
    u32 raw_offset,
    u32 characteristics)
{
    memcpy(bytes + offset, name, 8);
    link_write_u32(bytes, offset + 8, virtual_size);
    link_write_u32(bytes, offset + 12, virtual_address);
    link_write_u32(bytes, offset + 16, raw_size);
    link_write_u32(bytes, offset + 20, raw_offset);
    link_write_u32(
        bytes,
        offset + 36,
        characteristics);
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult
link_native_executable_elf64_aarch64(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
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
        0xf94003e0,
        0x910023e1,
        0x8b000c22,
        0x91002042,
        0x94000000,
        0xd2800ba8,
        0xd4000001,
        0xd4200000,
    };
    if (object->section_count <
            OBJECT_SECTION_COUNT ||
        !object->sections ||
        (object->symbol_count && !object->symbols) ||
        (object->relocation_count &&
            !object->relocations))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        if (object->symbols[symbol_index].section ==
            OBJECT_SECTION_UNDEFINED)
        {
            result.error =
                LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol =
                object->symbols[symbol_index].name;
            return result;
        }
    }
    String8 entry_name = options.entry_symbol.length ?
        options.entry_symbol :
        S8("main");
    u32 entry_symbol_index =
        link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX ||
        object->symbols[entry_symbol_index].section ==
            OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind !=
            OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    u32 program_header_count =
        object->sections[OBJECT_SECTION_DATA]
            .data.length ? 2 : 1;
    u64 header_end =
        ELF_HEADER_SIZE +
        (u64)program_header_count *
            ELF_PROGRAM_HEADER_SIZE;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 entry_stub_offset =
        align_forward(header_end, 16);
    section_offsets[OBJECT_SECTION_TEXT] =
        align_forward(
            entry_stub_offset +
                sizeof(entry_stub),
            object->sections[
                OBJECT_SECTION_TEXT].alignment);
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] =
        align_forward(
            section_offsets[OBJECT_SECTION_TEXT] +
                object->sections[
                    OBJECT_SECTION_TEXT].data.length,
            object->sections[
                OBJECT_SECTION_READ_ONLY_DATA]
                .alignment);
    u64 read_only_end =
        section_offsets[
            OBJECT_SECTION_READ_ONLY_DATA] +
        object->sections[
            OBJECT_SECTION_READ_ONLY_DATA]
            .data.length;
    section_offsets[OBJECT_SECTION_DATA] =
        align_forward(read_only_end, ELF_PAGE_SIZE);
    u64 file_size =
        object->sections[OBJECT_SECTION_DATA]
            .data.length ?
            section_offsets[OBJECT_SECTION_DATA] +
                object->sections[
                    OBJECT_SECTION_DATA].data.length :
            read_only_end;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(
            arena,
            u8,
            file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(
        bytes + entry_stub_offset,
        entry_stub,
        sizeof(entry_stub));
    for (u32 section = 0;
        section < OBJECT_SECTION_COUNT;
        section += 1)
    {
        ByteSlice data =
            object->sections[section].data;
        if (data.length)
        {
            memcpy(
                bytes + section_offsets[section],
                data.pointer,
                data.length);
        }
    }
    u64 image_base = 0x400000;
    ObjectSymbol* entry_symbol =
        &object->symbols[entry_symbol_index];
    u64 entry_address =
        image_base +
        section_offsets[entry_symbol->section] +
        entry_symbol->value;
    u64 call_offset =
        entry_stub_offset + 4 * sizeof(u32);
    s64 call_displacement =
        (s64)entry_address -
        (s64)(image_base + call_offset);
    s64 call_words = call_displacement / 4;
    if (call_displacement % 4 ||
        call_words < -(1 << 25) ||
        call_words >= (1 << 25))
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(
        bytes,
        call_offset,
        0x94000000 |
            ((u32)call_words & 0x03ffffff));
    for (u32 index = 0;
        index < object->relocation_count;
        index += 1)
    {
        ObjectRelocation* relocation =
            &object->relocations[index];
        if (relocation->section >=
                OBJECT_SECTION_COUNT ||
            relocation->symbol >=
                object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section =
            &object->sections[relocation->section];
        u64 width =
            relocation->kind ==
                OBJECT_RELOCATION_ABSOLUTE64 ?
                8 : 4;
        if (relocation->offset >
                section->data.length ||
            width >
                section->data.length -
                    relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol =
            &object->symbols[relocation->symbol];
        if (symbol->section >=
            OBJECT_SECTION_COUNT)
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 symbol_address =
            image_base +
            section_offsets[symbol->section] +
            symbol->value;
        u64 place_address =
            image_base +
            section_offsets[relocation->section] +
            relocation->offset;
        u64 output_offset =
            section_offsets[relocation->section] +
            relocation->offset;
        if (relocation->kind ==
            OBJECT_RELOCATION_AARCH64_CALL26)
        {
            s64 displacement =
                (s64)symbol_address +
                relocation->addend -
                (s64)place_address;
            s64 words = displacement / 4;
            if (displacement % 4 ||
                words < -(1 << 25) ||
                words >= (1 << 25))
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                output_offset,
                0x94000000 |
                    ((u32)words & 0x03ffffff));
        }
        else if (relocation->kind ==
            OBJECT_RELOCATION_ABSOLUTE64)
        {
            link_write_u64(
                bytes,
                output_offset,
                symbol_address +
                    (u64)relocation->addend);
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    link_write_u16(bytes, 16, 2);
    link_write_u16(
        bytes,
        18,
        ELF_MACHINE_AARCH64);
    link_write_u32(bytes, 20, 1);
    link_write_u64(
        bytes,
        24,
        image_base + entry_stub_offset);
    link_write_u64(bytes, 32, ELF_HEADER_SIZE);
    link_write_u16(bytes, 52, ELF_HEADER_SIZE);
    link_write_u16(
        bytes,
        54,
        ELF_PROGRAM_HEADER_SIZE);
    link_write_u16(
        bytes,
        56,
        (u16)program_header_count);
    u64 program_header = ELF_HEADER_SIZE;
    link_write_u32(bytes, program_header, 1);
    link_write_u32(bytes, program_header + 4, 5);
    link_write_u64(
        bytes,
        program_header + 16,
        image_base);
    link_write_u64(
        bytes,
        program_header + 24,
        image_base);
    link_write_u64(
        bytes,
        program_header + 32,
        read_only_end);
    link_write_u64(
        bytes,
        program_header + 40,
        read_only_end);
    link_write_u64(
        bytes,
        program_header + 48,
        ELF_PAGE_SIZE);
    if (program_header_count == 2)
    {
        program_header += ELF_PROGRAM_HEADER_SIZE;
        u64 data_size =
            object->sections[
                OBJECT_SECTION_DATA].data.length;
        link_write_u32(bytes, program_header, 1);
        link_write_u32(
            bytes,
            program_header + 4,
            6);
        link_write_u64(
            bytes,
            program_header + 8,
            section_offsets[OBJECT_SECTION_DATA]);
        link_write_u64(
            bytes,
            program_header + 16,
            image_base +
                section_offsets[
                    OBJECT_SECTION_DATA]);
        link_write_u64(
            bytes,
            program_header + 24,
            image_base +
                section_offsets[
                    OBJECT_SECTION_DATA]);
        link_write_u64(
            bytes,
            program_header + 32,
            data_size);
        link_write_u64(
            bytes,
            program_header + 40,
            data_size);
        link_write_u64(
            bytes,
            program_header + 48,
            ELF_PAGE_SIZE);
    }
    if (options.output_path.length &&
        !link_write_executable_file(
            options.output_path,
            result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 link_aarch64_adrp(
    u32 destination,
    u64 instruction_address,
    u64 target_address,
    bool* valid)
{
    s64 instruction_page =
        (s64)(instruction_address & ~UINT64_C(0xfff));
    s64 target_page =
        (s64)(target_address & ~UINT64_C(0xfff));
    s64 pages =
        (target_page - instruction_page) / 4096;
    if (destination > 31 ||
        pages < -(1 << 20) ||
        pages >= (1 << 20))
    {
        *valid = false;
        return 0;
    }
    u32 immediate = (u32)pages & 0x1fffff;
    return 0x90000000 |
        ((immediate & 3) << 29) |
        (((immediate >> 2) & 0x7ffff) << 5) |
        destination;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult
link_native_executable_elf64_aarch64_dynamic(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
{
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_PROGRAM_HEADER_COUNT = 6,
        ELF_DYNAMIC_SIZE = 16,
        ELF_DYNAMIC_COUNT = 12,
        ELF_RELOCATION_SIZE = 24,
        ELF_PLT_ENTRY_SIZE = 16,
        ELF_GOT_RESERVED_COUNT = 3,
    };
    static u32 const entry_stub[] = {
        0xf94003e0,
        0x910023e1,
        0x8b000c22,
        0x91002042,
        0x94000000,
        0xd2800ba8,
        0xd4000001,
        0xd4200000,
    };
    static char8 const interpreter[] =
        "/lib/ld-linux-aarch64.so.1";
    NativeExecutableLinkResult result = {0};
    ObjectRelocation* converted_relocations =
        arena_allocate(
            arena,
            ObjectRelocation,
            object->relocation_count);
    for (u32 index = 0;
        index < object->relocation_count;
        index += 1)
    {
        converted_relocations[index] =
            object->relocations[index];
        if (converted_relocations[index].kind ==
            OBJECT_RELOCATION_AARCH64_CALL26)
        {
            converted_relocations[index].kind =
                OBJECT_RELOCATION_X86_64_PC32;
        }
    }
    ObjectFile converted = *object;
    converted.relocations = converted_relocations;
    NativeExecutableLinkOptions staging_options =
        options;
    staging_options.output_path = (String8){0};
    result =
        link_native_executable_elf64_x86_64_dynamic(
            arena,
            &converted,
            staging_options);
    if (result.error != LINK_ERROR_NONE)
    {
        return result;
    }
    u8* bytes = result.executable.pointer;
    u64 image_base = 0x400000;
    u64 header_end =
        ELF_HEADER_SIZE +
        ELF_PROGRAM_HEADER_COUNT *
            ELF_PROGRAM_HEADER_SIZE;
    u64 entry_stub_offset =
        align_forward(header_end, 16);
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    section_offsets[OBJECT_SECTION_TEXT] =
        align_forward(
            entry_stub_offset + 35,
            object->sections[
                OBJECT_SECTION_TEXT].alignment);
    u64 plt_offset = align_forward(
        section_offsets[OBJECT_SECTION_TEXT] +
            object->sections[
                OBJECT_SECTION_TEXT].data.length,
        16);
    u32 import_count = 0;
    u32* import_indices = arena_allocate(
        arena,
        u32,
        object->symbol_count);
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        if (object->symbols[symbol_index].section ==
            OBJECT_SECTION_UNDEFINED)
        {
            import_indices[symbol_index] =
                import_count++;
        }
    }
    u64 plt_size =
        (u64)(import_count + 1) *
        ELF_PLT_ENTRY_SIZE;
    memset(bytes + plt_offset, 0, plt_size);
    memcpy(
        bytes + entry_stub_offset,
        entry_stub,
        sizeof(entry_stub));
    memcpy(
        bytes + link_read_u64(
            bytes,
            ELF_HEADER_SIZE +
                ELF_PROGRAM_HEADER_SIZE + 8),
        interpreter,
        sizeof(interpreter));
    link_write_u16(bytes, 18, 183);
    u64 dynamic_program_header =
        ELF_HEADER_SIZE +
        4 * ELF_PROGRAM_HEADER_SIZE;
    u64 dynamic_offset =
        link_read_u64(
            bytes,
            dynamic_program_header + 8);
    u64 relocation_offset = 0;
    for (u32 index = 0;
        index < ELF_DYNAMIC_COUNT;
        index += 1)
    {
        u64 entry =
            dynamic_offset +
            (u64)index * ELF_DYNAMIC_SIZE;
        u64 tag = link_read_u64(bytes, entry);
        if (tag == 3)
        {
            link_write_u64(bytes, entry, 24);
            link_write_u64(bytes, entry + 8, 0);
        }
        else if (tag == 23)
        {
            u64 address =
                link_read_u64(bytes, entry + 8);
            relocation_offset =
                address - image_base;
        }
    }
    if (!relocation_offset)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    bool valid = true;
    for (u32 import_index = 0;
        import_index < import_count;
        import_index += 1)
    {
        u64 relocation =
            relocation_offset +
            (u64)import_index *
                ELF_RELOCATION_SIZE;
        u64 slot_address =
            link_read_u64(bytes, relocation);
        link_write_u64(
            bytes,
            relocation + 8,
            ((u64)(import_index + 1) << 32) |
                1026);
        u64 thunk_offset =
            plt_offset +
            (u64)(import_index + 1) *
                ELF_PLT_ENTRY_SIZE;
        u64 thunk_address =
            image_base + thunk_offset;
        u64 page_offset =
            slot_address & 0xfff;
        if (page_offset % 8 ||
            page_offset / 8 > 0xfff)
        {
            valid = false;
            break;
        }
        link_write_u32(
            bytes,
            thunk_offset,
            link_aarch64_adrp(
                16,
                thunk_address,
                slot_address,
                &valid));
        link_write_u32(
            bytes,
            thunk_offset + 4,
            0xf9400000 |
                ((u32)(page_offset / 8) << 10) |
                (16 << 5) |
                17);
        link_write_u32(
            bytes,
            thunk_offset + 8,
            0xd61f0220);
        link_write_u32(
            bytes,
            thunk_offset + 12,
            0xd503201f);
        link_write_u64(
            bytes,
            (slot_address - image_base),
            0);
    }
    String8 entry_name = options.entry_symbol.length ?
        options.entry_symbol :
        S8("main");
    u32 entry_symbol_index =
        link_symbol_find(object, entry_name);
    if (!valid ||
        entry_symbol_index == UINT32_MAX)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    ObjectSymbol* entry_symbol =
        &object->symbols[entry_symbol_index];
    u64 entry_address =
        image_base +
        section_offsets[entry_symbol->section] +
        entry_symbol->value;
    u64 call_offset =
        entry_stub_offset + 4 * sizeof(u32);
    s64 call_displacement =
        (s64)entry_address -
        (s64)(image_base + call_offset);
    s64 call_words = call_displacement / 4;
    if (call_displacement % 4 ||
        call_words < -(1 << 25) ||
        call_words >= (1 << 25))
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    link_write_u32(
        bytes,
        call_offset,
        0x94000000 |
            ((u32)call_words & 0x03ffffff));
    for (u32 index = 0;
        index < object->relocation_count;
        index += 1)
    {
        ObjectRelocation* relocation =
            &object->relocations[index];
        if (relocation->kind !=
            OBJECT_RELOCATION_AARCH64_CALL26)
        {
            continue;
        }
        ObjectSymbol* symbol =
            &object->symbols[relocation->symbol];
        u64 symbol_address = 0;
        if (symbol->section ==
            OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index =
                import_indices[relocation->symbol];
            if (import_index == UINT32_MAX)
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_address =
                image_base + plt_offset +
                (u64)(import_index + 1) *
                    ELF_PLT_ENTRY_SIZE;
        }
        else
        {
            symbol_address =
                image_base +
                section_offsets[symbol->section] +
                symbol->value;
        }
        u64 output_offset =
            section_offsets[relocation->section] +
            relocation->offset;
        u64 place_address =
            image_base + output_offset;
        s64 displacement =
            (s64)symbol_address +
            relocation->addend -
            (s64)place_address;
        s64 words = displacement / 4;
        if (displacement % 4 ||
            words < -(1 << 25) ||
            words >= (1 << 25))
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(
            bytes,
            output_offset,
            0x94000000 |
                ((u32)words & 0x03ffffff));
    }
    if (options.output_path.length &&
        !link_write_executable_file(
            options.output_path,
            result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult
link_native_executable_pe64(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        PE_OFFSET = 0x80,
        PE_COFF_HEADER_SIZE = 20,
        PE_OPTIONAL_HEADER_SIZE = 240,
        PE_SECTION_HEADER_SIZE = 40,
        PE_SECTION_COUNT = 4,
        PE_FILE_ALIGNMENT = 0x200,
        PE_SECTION_ALIGNMENT = 0x1000,
        PE_IMPORT_DESCRIPTOR_SIZE = 20,
        PE_IMAGE_BASE_LOW = 0x40000000,
        PE_IMAGE_BASE_HIGH = 1,
    };
    static u8 const entry_stub_x86_64[] = {
        0x48, 0x83, 0xec, 0x28,
        0xe8, 0, 0, 0, 0,
        0x89, 0xc1,
        0xff, 0x15, 0, 0, 0, 0,
        0xcc,
    };
    static u32 const entry_stub_aarch64[] = {
        0xa9bf7bfd,
        0x910003fd,
        0x94000000,
        0x94000000,
        0xd4200000,
    };
    static char8 const runtime_library[] =
        "ucrtbase.dll";
    static char8 const kernel_library[] =
        "kernel32.dll";
    static char8 const exit_name[] =
        "ExitProcess";
    bool aarch64 =
        object->target.cpu_arch ==
        CPU_ARCH_AARCH64;
    if (object->section_count <
            OBJECT_SECTION_COUNT ||
        !object->sections ||
        (object->symbol_count && !object->symbols) ||
        (object->relocation_count &&
            !object->relocations) ||
        (object->target.cpu_arch != CPU_ARCH_X86_64 &&
            !aarch64))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u32 import_count = 0;
    u64 imported_name_size = 0;
    u32* import_indices = arena_allocate(
        arena,
        u32,
        object->symbol_count);
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        ObjectSymbol* symbol =
            &object->symbols[symbol_index];
        if (symbol->section !=
            OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        if (!symbol->global ||
            symbol->kind != OBJECT_SYMBOL_FUNCTION ||
            !symbol->name.length ||
            symbol->name.length > UINT32_MAX ||
            imported_name_size >
                UINT32_MAX - symbol->name.length - 3)
        {
            result.error =
                LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = symbol->name;
            return result;
        }
        import_indices[symbol_index] = import_count;
        imported_name_size +=
            align_forward(
                symbol->name.length + 3,
                2);
        import_count += 1;
    }
    String8 entry_name = options.entry_symbol.length ?
        options.entry_symbol :
        S8("main");
    u32 entry_symbol_index =
        link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX ||
        object->symbols[entry_symbol_index].section ==
            OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind !=
            OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    u64 header_size = align_forward(
        PE_OFFSET + 4 + PE_COFF_HEADER_SIZE +
            PE_OPTIONAL_HEADER_SIZE +
            PE_SECTION_COUNT *
                PE_SECTION_HEADER_SIZE,
        PE_FILE_ALIGNMENT);
    u32 section_rvas[PE_SECTION_COUNT] = {
        PE_SECTION_ALIGNMENT,
    };
    u32 section_raw_offsets[PE_SECTION_COUNT] = {
        (u32)header_size,
    };
    u64 object_section_offsets[OBJECT_SECTION_COUNT] = {0};
    u64 entry_stub_offset = 0;
    u64 entry_stub_size = aarch64 ?
        sizeof(entry_stub_aarch64) :
        sizeof(entry_stub_x86_64);
    object_section_offsets[OBJECT_SECTION_TEXT] =
        align_forward(
            entry_stub_size,
            object->sections[
                OBJECT_SECTION_TEXT].alignment);
    u64 thunk_offset = align_forward(
        object_section_offsets[OBJECT_SECTION_TEXT] +
            object->sections[
                OBJECT_SECTION_TEXT].data.length,
        16);
    u32 thunk_entry_size = aarch64 ? 16 : 6;
    u64 thunk_size =
        (u64)(import_count + (aarch64 ? 1 : 0)) *
        thunk_entry_size;
    u64 text_virtual_size =
        thunk_offset + thunk_size;
    u64 text_raw_size = align_forward(
        text_virtual_size,
        PE_FILE_ALIGNMENT);
    section_rvas[1] = (u32)align_forward(
        section_rvas[0] + text_virtual_size,
        PE_SECTION_ALIGNMENT);
    section_raw_offsets[1] =
        section_raw_offsets[0] +
        (u32)text_raw_size;
    object_section_offsets[
        OBJECT_SECTION_READ_ONLY_DATA] = 0;
    u64 read_only_virtual_size =
        object->sections[
            OBJECT_SECTION_READ_ONLY_DATA]
            .data.length;
    u64 read_only_raw_size = align_forward(
        BUSTER_MAX(read_only_virtual_size, 1),
        PE_FILE_ALIGNMENT);
    section_rvas[2] = (u32)align_forward(
        section_rvas[1] +
            BUSTER_MAX(read_only_virtual_size, 1),
        PE_SECTION_ALIGNMENT);
    section_raw_offsets[2] =
        section_raw_offsets[1] +
        (u32)read_only_raw_size;
    object_section_offsets[OBJECT_SECTION_DATA] = 0;
    u64 data_virtual_size =
        object->sections[
            OBJECT_SECTION_DATA].data.length;
    u64 data_raw_size = align_forward(
        BUSTER_MAX(data_virtual_size, 1),
        PE_FILE_ALIGNMENT);
    section_rvas[3] = (u32)align_forward(
        section_rvas[2] +
            BUSTER_MAX(data_virtual_size, 1),
        PE_SECTION_ALIGNMENT);
    section_raw_offsets[3] =
        section_raw_offsets[2] +
        (u32)data_raw_size;
    u32 import_descriptor_count =
        import_count ? 3 : 2;
    u64 import_descriptor_offset = 0;
    u64 runtime_lookup_offset =
        align_forward(
            (u64)import_descriptor_count *
                PE_IMPORT_DESCRIPTOR_SIZE,
            8);
    u64 runtime_address_offset =
        runtime_lookup_offset +
        (u64)(import_count + 1) * sizeof(u64);
    u64 kernel_lookup_offset =
        runtime_address_offset +
        (u64)(import_count + 1) * sizeof(u64);
    u64 kernel_address_offset =
        kernel_lookup_offset + 2 * sizeof(u64);
    u64 import_name_offset =
        kernel_address_offset + 2 * sizeof(u64);
    u64 runtime_library_offset =
        import_name_offset + imported_name_size;
    u64 kernel_library_offset =
        runtime_library_offset +
        sizeof(runtime_library);
    u64 exit_import_name_offset = align_forward(
        kernel_library_offset +
            sizeof(kernel_library),
        2);
    u64 import_virtual_size =
        exit_import_name_offset +
        2 + sizeof(exit_name);
    u64 import_raw_size = align_forward(
        import_virtual_size,
        PE_FILE_ALIGNMENT);
    u64 file_size =
        section_raw_offsets[3] + import_raw_size;
    u64 image_size = align_forward(
        section_rvas[3] + import_virtual_size,
        PE_SECTION_ALIGNMENT);
    if (file_size > UINT32_MAX ||
        image_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(
            arena,
            u8,
            file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    memcpy(
        bytes + section_raw_offsets[0] +
            entry_stub_offset,
        aarch64 ?
            (void const*)entry_stub_aarch64 :
            (void const*)entry_stub_x86_64,
        entry_stub_size);
    for (u32 section = 0;
        section < OBJECT_SECTION_COUNT;
        section += 1)
    {
        u32 output_section = section;
        ByteSlice data =
            object->sections[section].data;
        if (data.length)
        {
            memcpy(
                bytes +
                    section_raw_offsets[
                        output_section] +
                    object_section_offsets[section],
                data.pointer,
                data.length);
        }
    }
    u32 import_section_rva = section_rvas[3];
    u64 import_section_raw =
        section_raw_offsets[3];
    u64 imported_name_cursor =
        import_name_offset;
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        u32 import_index =
            import_indices[symbol_index];
        if (import_index == UINT32_MAX)
        {
            continue;
        }
        ObjectSymbol* symbol =
            &object->symbols[symbol_index];
        u32 name_rva =
            import_section_rva +
            (u32)imported_name_cursor;
        link_write_u64(
            bytes,
            import_section_raw +
                runtime_lookup_offset +
                (u64)import_index * sizeof(u64),
            name_rva);
        link_write_u64(
            bytes,
            import_section_raw +
                runtime_address_offset +
                (u64)import_index * sizeof(u64),
            name_rva);
        memcpy(
            bytes + import_section_raw +
                imported_name_cursor + 2,
            symbol->name.pointer,
            symbol->name.length);
        imported_name_cursor += align_forward(
            symbol->name.length + 3,
            2);
        u64 thunk_raw =
            section_raw_offsets[0] +
            thunk_offset +
            (u64)import_index *
                thunk_entry_size;
        u64 thunk_rva =
            section_rvas[0] +
            thunk_offset +
            (u64)import_index *
                thunk_entry_size;
        u64 iat_rva =
            import_section_rva +
            runtime_address_offset +
            (u64)import_index * sizeof(u64);
        if (!aarch64)
        {
            bytes[thunk_raw] = 0xff;
            bytes[thunk_raw + 1] = 0x25;
            link_write_u32(
                bytes,
                thunk_raw + 2,
                (u32)(s32)(
                    (s64)iat_rva -
                    (s64)(thunk_rva + 6)));
        }
        else
        {
            u64 slot_address =
                (((u64)PE_IMAGE_BASE_HIGH << 32) |
                    PE_IMAGE_BASE_LOW) +
                iat_rva;
            u64 thunk_address =
                (((u64)PE_IMAGE_BASE_HIGH << 32) |
                    PE_IMAGE_BASE_LOW) +
                thunk_rva;
            u64 page_offset =
                slot_address & 0xfff;
            bool adrp_valid = true;
            link_write_u32(
                bytes,
                thunk_raw,
                link_aarch64_adrp(
                    16,
                    thunk_address,
                    slot_address,
                    &adrp_valid));
            if (!adrp_valid || page_offset % 8)
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                thunk_raw + 4,
                0xf9400000 |
                    ((u32)(page_offset / 8) << 10) |
                    (16 << 5) |
                    17);
            link_write_u32(
                bytes,
                thunk_raw + 8,
                0xd61f0220);
            link_write_u32(
                bytes,
                thunk_raw + 12,
                0xd503201f);
        }
    }
    memcpy(
        bytes + import_section_raw +
            runtime_library_offset,
        runtime_library,
        sizeof(runtime_library));
    memcpy(
        bytes + import_section_raw +
            kernel_library_offset,
        kernel_library,
        sizeof(kernel_library));
    memcpy(
        bytes + import_section_raw +
            exit_import_name_offset + 2,
        exit_name,
        sizeof(exit_name));
    u32 exit_name_rva =
        import_section_rva +
        (u32)exit_import_name_offset;
    link_write_u64(
        bytes,
        import_section_raw +
            kernel_lookup_offset,
        exit_name_rva);
    link_write_u64(
        bytes,
        import_section_raw +
            kernel_address_offset,
        exit_name_rva);
    u64 descriptor = import_section_raw +
        import_descriptor_offset;
    if (import_count)
    {
        link_write_u32(
            bytes,
            descriptor,
            import_section_rva +
                (u32)runtime_lookup_offset);
        link_write_u32(
            bytes,
            descriptor + 12,
            import_section_rva +
                (u32)runtime_library_offset);
        link_write_u32(
            bytes,
            descriptor + 16,
            import_section_rva +
                (u32)runtime_address_offset);
        descriptor += PE_IMPORT_DESCRIPTOR_SIZE;
    }
    link_write_u32(
        bytes,
        descriptor,
        import_section_rva +
            (u32)kernel_lookup_offset);
    link_write_u32(
        bytes,
        descriptor + 12,
        import_section_rva +
            (u32)kernel_library_offset);
    link_write_u32(
        bytes,
        descriptor + 16,
        import_section_rva +
            (u32)kernel_address_offset);
    u64 entry_rva =
        section_rvas[0] + entry_stub_offset;
    ObjectSymbol* entry_symbol =
        &object->symbols[entry_symbol_index];
    u64 main_rva =
        section_rvas[entry_symbol->section] +
        object_section_offsets[entry_symbol->section] +
        entry_symbol->value;
    u64 exit_iat_rva =
        import_section_rva +
        kernel_address_offset;
    if (!aarch64)
    {
        s64 main_displacement =
            (s64)main_rva -
            (s64)(entry_rva + 9);
        s64 exit_displacement =
            (s64)exit_iat_rva -
            (s64)(entry_rva + 17);
        if (main_displacement < INT32_MIN ||
            main_displacement > INT32_MAX ||
            exit_displacement < INT32_MIN ||
            exit_displacement > INT32_MAX)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(
            bytes,
            section_raw_offsets[0] + 5,
            (u32)(s32)main_displacement);
        link_write_u32(
            bytes,
            section_raw_offsets[0] + 13,
            (u32)(s32)exit_displacement);
    }
    else
    {
        u64 exit_thunk_rva =
            section_rvas[0] + thunk_offset +
            (u64)import_count * thunk_entry_size;
        u64 exit_thunk_raw =
            section_raw_offsets[0] + thunk_offset +
            (u64)import_count * thunk_entry_size;
        u64 image_base =
            ((u64)PE_IMAGE_BASE_HIGH << 32) |
            PE_IMAGE_BASE_LOW;
        u64 exit_iat_address =
            image_base + exit_iat_rva;
        u64 exit_thunk_address =
            image_base + exit_thunk_rva;
        u64 page_offset =
            exit_iat_address & 0xfff;
        bool adrp_valid = true;
        link_write_u32(
            bytes,
            exit_thunk_raw,
            link_aarch64_adrp(
                16,
                exit_thunk_address,
                exit_iat_address,
                &adrp_valid));
        if (!adrp_valid || page_offset % 8)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(
            bytes,
            exit_thunk_raw + 4,
            0xf9400000 |
                ((u32)(page_offset / 8) << 10) |
                (16 << 5) |
                17);
        link_write_u32(
            bytes,
            exit_thunk_raw + 8,
            0xd61f0220);
        link_write_u32(
            bytes,
            exit_thunk_raw + 12,
            0xd503201f);
        s64 main_words =
            ((s64)main_rva -
                (s64)(entry_rva + 8)) / 4;
        s64 exit_words =
            ((s64)exit_thunk_rva -
                (s64)(entry_rva + 12)) / 4;
        if (main_words < -(1 << 25) ||
            main_words >= (1 << 25) ||
            exit_words < -(1 << 25) ||
            exit_words >= (1 << 25))
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        link_write_u32(
            bytes,
            section_raw_offsets[0] + 8,
            0x94000000 |
                ((u32)main_words & 0x03ffffff));
        link_write_u32(
            bytes,
            section_raw_offsets[0] + 12,
            0x94000000 |
                ((u32)exit_words & 0x03ffffff));
    }
    for (u32 relocation_index = 0;
        relocation_index < object->relocation_count;
        relocation_index += 1)
    {
        ObjectRelocation* relocation =
            &object->relocations[relocation_index];
        if (relocation->section >=
                OBJECT_SECTION_COUNT ||
            relocation->symbol >=
                object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section =
            &object->sections[relocation->section];
        u64 width =
            relocation->kind ==
                OBJECT_RELOCATION_ABSOLUTE64 ?
                8 : 4;
        if (relocation->offset >
                section->data.length ||
            width >
                section->data.length -
                    relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol =
            &object->symbols[relocation->symbol];
        u64 symbol_rva = 0;
        if (symbol->section ==
            OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index =
                import_indices[relocation->symbol];
            bool call_relocation =
                (!aarch64 &&
                    relocation->kind ==
                        OBJECT_RELOCATION_X86_64_PC32) ||
                (aarch64 &&
                    relocation->kind ==
                        OBJECT_RELOCATION_AARCH64_CALL26);
            if (import_index == UINT32_MAX ||
                !call_relocation)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_rva =
                section_rvas[0] +
                thunk_offset +
                (u64)import_index *
                    thunk_entry_size;
        }
        else if (symbol->section <
            OBJECT_SECTION_COUNT)
        {
            symbol_rva =
                section_rvas[symbol->section] +
                object_section_offsets[symbol->section] +
                symbol->value;
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 place_rva =
            section_rvas[relocation->section] +
            object_section_offsets[
                relocation->section] +
            relocation->offset;
        u64 output_offset =
            section_raw_offsets[relocation->section] +
            object_section_offsets[
                relocation->section] +
            relocation->offset;
        if (relocation->kind ==
            OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 value =
                (s64)symbol_rva +
                relocation->addend -
                (s64)place_rva;
            if (value < INT32_MIN ||
                value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                output_offset,
                (u32)(s32)value);
        }
        else if (relocation->kind ==
            OBJECT_RELOCATION_AARCH64_CALL26)
        {
            s64 displacement =
                (s64)symbol_rva +
                relocation->addend -
                (s64)place_rva;
            s64 words = displacement / 4;
            if (displacement % 4 ||
                words < -(1 << 25) ||
                words >= (1 << 25))
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                output_offset,
                0x94000000 |
                    ((u32)words & 0x03ffffff));
        }
        else if (relocation->kind ==
            OBJECT_RELOCATION_ABSOLUTE64)
        {
            u64 image_base =
                ((u64)PE_IMAGE_BASE_HIGH << 32) |
                PE_IMAGE_BASE_LOW;
            link_write_u64(
                bytes,
                output_offset,
                image_base + symbol_rva +
                    (u64)relocation->addend);
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
    link_write_u16(
        bytes,
        coff,
        aarch64 ? 0xaa64 : 0x8664);
    link_write_u16(bytes, coff + 2, PE_SECTION_COUNT);
    link_write_u16(
        bytes,
        coff + 16,
        PE_OPTIONAL_HEADER_SIZE);
    link_write_u16(bytes, coff + 18, 0x23);
    u64 optional = coff + PE_COFF_HEADER_SIZE;
    link_write_u16(bytes, optional, 0x20b);
    link_write_u32(
        bytes,
        optional + 4,
        (u32)text_raw_size);
    link_write_u32(
        bytes,
        optional + 8,
        (u32)(read_only_raw_size +
            data_raw_size + import_raw_size));
    link_write_u32(
        bytes,
        optional + 16,
        (u32)entry_rva);
    link_write_u32(
        bytes,
        optional + 20,
        section_rvas[0]);
    link_write_u32(
        bytes,
        optional + 24,
        PE_IMAGE_BASE_LOW);
    link_write_u32(
        bytes,
        optional + 28,
        PE_IMAGE_BASE_HIGH);
    link_write_u32(
        bytes,
        optional + 32,
        PE_SECTION_ALIGNMENT);
    link_write_u32(
        bytes,
        optional + 36,
        PE_FILE_ALIGNMENT);
    link_write_u16(bytes, optional + 40, 6);
    link_write_u16(bytes, optional + 48, 6);
    link_write_u32(
        bytes,
        optional + 56,
        (u32)image_size);
    link_write_u32(
        bytes,
        optional + 60,
        (u32)header_size);
    link_write_u16(bytes, optional + 68, 3);
    link_write_u16(bytes, optional + 70, 0x100);
    link_write_u64(
        bytes,
        optional + 72,
        1024 * 1024);
    link_write_u64(bytes, optional + 80, 4096);
    link_write_u64(
        bytes,
        optional + 88,
        1024 * 1024);
    link_write_u64(bytes, optional + 96, 4096);
    link_write_u32(bytes, optional + 108, 16);
    link_write_u32(
        bytes,
        optional + 120,
        import_section_rva);
    link_write_u32(
        bytes,
        optional + 124,
        (u32)(import_descriptor_count *
            PE_IMPORT_DESCRIPTOR_SIZE));
    link_write_u32(
        bytes,
        optional + 208,
        import_section_rva +
            (u32)(import_count ?
                runtime_address_offset :
                kernel_address_offset));
    link_write_u32(
        bytes,
        optional + 212,
        (u32)(import_count ?
            ((u64)import_count + 5) *
                sizeof(u64) :
            2 * sizeof(u64)));
    u64 section_header =
        optional + PE_OPTIONAL_HEADER_SIZE;
    link_pe_section_header(
        bytes,
        section_header,
        ".text\0\0\0",
        (u32)text_virtual_size,
        section_rvas[0],
        (u32)text_raw_size,
        section_raw_offsets[0],
        0x60000020);
    link_pe_section_header(
        bytes,
        section_header + PE_SECTION_HEADER_SIZE,
        ".rdata\0\0",
        (u32)BUSTER_MAX(
            read_only_virtual_size,
            1),
        section_rvas[1],
        (u32)read_only_raw_size,
        section_raw_offsets[1],
        0x40000040);
    link_pe_section_header(
        bytes,
        section_header +
            2 * PE_SECTION_HEADER_SIZE,
        ".data\0\0\0",
        (u32)BUSTER_MAX(
            data_virtual_size,
            1),
        section_rvas[2],
        (u32)data_raw_size,
        section_raw_offsets[2],
        0xc0000040);
    link_pe_section_header(
        bytes,
        section_header +
            3 * PE_SECTION_HEADER_SIZE,
        ".idata\0\0",
        (u32)import_virtual_size,
        section_rvas[3],
        (u32)import_raw_size,
        section_raw_offsets[3],
        0xc0000040);
    if (options.output_path.length &&
        !link_write_executable_file(
            options.output_path,
            result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void link_mach_name_write(
    u8* bytes,
    u64 offset,
    char const* name)
{
    u64 length = strlen(name);
    memcpy(
        bytes + offset,
        name,
        BUSTER_MIN(length, 16));
}

BUSTER_GLOBAL_LOCAL u64 link_uleb128_write(
    u8* bytes,
    u64 offset,
    u64 value)
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

BUSTER_GLOBAL_LOCAL void link_mach_section_write(
    u8* bytes,
    u64 offset,
    char const* section_name,
    char const* segment_name,
    u64 address,
    u64 size,
    u32 file_offset,
    u32 alignment,
    u32 flags,
    u32 reserved1,
    u32 reserved2)
{
    link_mach_name_write(
        bytes,
        offset,
        section_name);
    link_mach_name_write(
        bytes,
        offset + 16,
        segment_name);
    link_write_u64(bytes, offset + 32, address);
    link_write_u64(bytes, offset + 40, size);
    link_write_u32(bytes, offset + 48, file_offset);
    link_write_u32(bytes, offset + 52, alignment);
    link_write_u32(bytes, offset + 64, flags);
    link_write_u32(bytes, offset + 68, reserved1);
    link_write_u32(bytes, offset + 72, reserved2);
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult
link_native_executable_mach_o64(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    enum
    {
        MACH_HEADER_SIZE = 32,
        MACH_SEGMENT_COMMAND_SIZE = 72,
        MACH_SECTION_SIZE = 80,
        MACH_TEXT_COMMAND_SIZE = 152,
        MACH_DATA_COMMAND_SIZE = 232,
        MACH_DYLD_INFO_COMMAND_SIZE = 48,
        MACH_DYLINKER_COMMAND_SIZE = 32,
        MACH_DYLIB_COMMAND_SIZE = 56,
        MACH_MAIN_COMMAND_SIZE = 24,
        MACH_BUILD_VERSION_COMMAND_SIZE = 24,
        MACH_SYMTAB_COMMAND_SIZE = 24,
        MACH_DYSYMTAB_COMMAND_SIZE = 80,
        MACH_CODE_SIGNATURE_COMMAND_SIZE = 16,
        MACH_COMMAND_COUNT = 12,
        MACH_PAGE_SIZE = 0x4000,
        MACH_CODE_PAGE_SIZE = 0x1000,
        MACH_CODE_DIRECTORY_HEADER_SIZE = 48,
        MACH_CODE_DIRECTORY_IDENTIFIER_SIZE = 7,
        MACH_NLIST_SIZE = 16,
        MACH_STUB_X86_64_SIZE = 6,
        MACH_STUB_AARCH64_SIZE = 16,
    };
    static char8 const dyld_path[] =
        "/usr/lib/dyld";
    static char8 const system_path[] =
        "/usr/lib/libSystem.B.dylib";
    if (object->section_count <
            OBJECT_SECTION_COUNT ||
        !object->sections ||
        (object->symbol_count && !object->symbols) ||
        (object->relocation_count &&
            !object->relocations) ||
        (object->target.cpu_arch != CPU_ARCH_X86_64 &&
            object->target.cpu_arch !=
                CPU_ARCH_AARCH64))
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u32 import_count = 0;
    u64 import_name_size = 0;
    u32* import_indices = arena_allocate(
        arena,
        u32,
        object->symbol_count);
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        import_indices[symbol_index] = UINT32_MAX;
        ObjectSymbol* symbol =
            &object->symbols[symbol_index];
        if (symbol->section !=
            OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        if (!symbol->global ||
            symbol->kind != OBJECT_SYMBOL_FUNCTION ||
            !symbol->name.length)
        {
            result.error =
                LINK_ERROR_UNRESOLVED_SYMBOL;
            result.symbol = symbol->name;
            return result;
        }
        import_indices[symbol_index] =
            import_count++;
        import_name_size +=
            symbol->name.length + 2;
    }
    String8 entry_name = options.entry_symbol.length ?
        options.entry_symbol :
        S8("main");
    u32 entry_symbol_index =
        link_symbol_find(object, entry_name);
    if (entry_symbol_index == UINT32_MAX ||
        object->symbols[entry_symbol_index].section ==
            OBJECT_SECTION_UNDEFINED ||
        object->symbols[entry_symbol_index].kind !=
            OBJECT_SYMBOL_FUNCTION)
    {
        result.error = LINK_ERROR_ENTRY_SYMBOL;
        result.symbol = entry_name;
        return result;
    }
    u64 command_size =
        2 * MACH_SEGMENT_COMMAND_SIZE +
        MACH_TEXT_COMMAND_SIZE +
        MACH_DATA_COMMAND_SIZE +
        MACH_DYLD_INFO_COMMAND_SIZE +
        MACH_DYLINKER_COMMAND_SIZE +
        MACH_DYLIB_COMMAND_SIZE +
        MACH_MAIN_COMMAND_SIZE +
        MACH_BUILD_VERSION_COMMAND_SIZE +
        MACH_SYMTAB_COMMAND_SIZE +
        MACH_DYSYMTAB_COMMAND_SIZE +
        MACH_CODE_SIGNATURE_COMMAND_SIZE;
    u64 header_end =
        MACH_HEADER_SIZE + command_size;
    u64 section_offsets[OBJECT_SECTION_COUNT] = {0};
    section_offsets[OBJECT_SECTION_TEXT] =
        align_forward(
            header_end,
            object->sections[
                OBJECT_SECTION_TEXT].alignment);
    u32 stub_size =
        object->target.cpu_arch ==
                CPU_ARCH_X86_64 ?
            MACH_STUB_X86_64_SIZE :
            MACH_STUB_AARCH64_SIZE;
    u64 stub_offset = align_forward(
        section_offsets[OBJECT_SECTION_TEXT] +
            object->sections[
                OBJECT_SECTION_TEXT].data.length,
        16);
    section_offsets[OBJECT_SECTION_READ_ONLY_DATA] =
        align_forward(
            stub_offset +
                (u64)import_count * stub_size,
            object->sections[
                OBJECT_SECTION_READ_ONLY_DATA]
                .alignment);
    u64 text_end =
        section_offsets[
            OBJECT_SECTION_READ_ONLY_DATA] +
        object->sections[
            OBJECT_SECTION_READ_ONLY_DATA]
            .data.length;
    u64 data_file_offset =
        align_forward(text_end, MACH_PAGE_SIZE);
    section_offsets[OBJECT_SECTION_DATA] =
        data_file_offset;
    u64 got_offset = align_forward(
        section_offsets[OBJECT_SECTION_DATA] +
            object->sections[
                OBJECT_SECTION_DATA].data.length,
        8);
    u64 data_end =
        got_offset +
        (u64)import_count * sizeof(u64);
    u64 linkedit_offset =
        align_forward(
            BUSTER_MAX(
                data_end,
                data_file_offset + 1),
            MACH_PAGE_SIZE);
    u32 rebase_count = 0;
    for (u32 relocation_index = 0;
        relocation_index < object->relocation_count;
        relocation_index += 1)
    {
        if (object->relocations[relocation_index].kind ==
            OBJECT_RELOCATION_ABSOLUTE64)
        {
            rebase_count += 1;
        }
    }
    u64 rebase_capacity =
        rebase_count ?
            2 + (u64)rebase_count * 12 :
            0;
    u8* rebase_bytes = arena_allocate(
        arena,
        u8,
        rebase_capacity);
    u64 rebase_size = 0;
    if (rebase_count)
    {
        rebase_bytes[rebase_size++] = 0x11;
        for (u32 relocation_index = 0;
            relocation_index <
                object->relocation_count;
            relocation_index += 1)
        {
            ObjectRelocation* relocation =
                &object->relocations[
                    relocation_index];
            if (relocation->kind !=
                OBJECT_RELOCATION_ABSOLUTE64)
            {
                continue;
            }
            if (relocation->section >=
                OBJECT_SECTION_COUNT)
            {
                result.error =
                    LINK_ERROR_RELOCATION;
                return result;
            }
            u8 segment_index =
                relocation->section ==
                        OBJECT_SECTION_DATA ?
                    2 : 1;
            u64 segment_offset =
                relocation->section ==
                        OBJECT_SECTION_DATA ?
                    section_offsets[
                        relocation->section] +
                        relocation->offset -
                        data_file_offset :
                    section_offsets[
                        relocation->section] +
                        relocation->offset;
            rebase_bytes[rebase_size++] =
                (u8)(0x20 | segment_index);
            rebase_size = link_uleb128_write(
                rebase_bytes,
                rebase_size,
                segment_offset);
            rebase_bytes[rebase_size++] = 0x51;
        }
        rebase_bytes[rebase_size++] = 0;
    }
    u64 bind_capacity =
        import_name_size +
        (u64)import_count * 20 + 4;
    u8* bind_bytes = arena_allocate(
        arena,
        u8,
        bind_capacity);
    u64 bind_size = 0;
    if (import_count)
    {
        bind_bytes[bind_size++] = 0x11;
        bind_bytes[bind_size++] = 0x51;
        bind_bytes[bind_size++] = 0x72;
        bind_size = link_uleb128_write(
            bind_bytes,
            bind_size,
            got_offset - data_file_offset);
        for (u32 symbol_index = 0;
            symbol_index < object->symbol_count;
            symbol_index += 1)
        {
            if (import_indices[symbol_index] ==
                UINT32_MAX)
            {
                continue;
            }
            ObjectSymbol* symbol =
                &object->symbols[symbol_index];
            bind_bytes[bind_size++] = 0x40;
            bind_bytes[bind_size++] = '_';
            memcpy(
                bind_bytes + bind_size,
                symbol->name.pointer,
                symbol->name.length);
            bind_size += symbol->name.length;
            bind_bytes[bind_size++] = 0;
            bind_bytes[bind_size++] = 0x90;
        }
        bind_bytes[bind_size++] = 0;
    }
    u64 bind_offset =
        linkedit_offset + rebase_size;
    u64 symbol_table_offset = align_forward(
        bind_offset + bind_size,
        8);
    u64 symbol_table_size =
        (u64)import_count * MACH_NLIST_SIZE;
    u64 string_table_offset =
        symbol_table_offset + symbol_table_size;
    u64 string_table_size =
        1 + import_name_size;
    u64 unsigned_file_size =
        string_table_offset + string_table_size;
    u64 signature_offset =
        align_forward(unsigned_file_size, 16);
    u64 code_slot_count =
        (signature_offset +
            MACH_CODE_PAGE_SIZE - 1) /
        MACH_CODE_PAGE_SIZE;
    u64 code_directory_size =
        MACH_CODE_DIRECTORY_HEADER_SIZE +
        MACH_CODE_DIRECTORY_IDENTIFIER_SIZE +
        code_slot_count * 32;
    u64 signature_size =
        20 + code_directory_size;
    u64 file_size =
        signature_offset + signature_size;
    if (file_size > UINT32_MAX)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    result.executable = (ByteSlice){
        .pointer = arena_allocate(
            arena,
            u8,
            file_size),
        .length = file_size,
    };
    u8* bytes = result.executable.pointer;
    memset(bytes, 0, file_size);
    for (u32 section = 0;
        section < OBJECT_SECTION_COUNT;
        section += 1)
    {
        ByteSlice data =
            object->sections[section].data;
        if (data.length)
        {
            memcpy(
                bytes + section_offsets[section],
                data.pointer,
                data.length);
        }
    }
    if (rebase_size)
    {
        memcpy(
            bytes + linkedit_offset,
            rebase_bytes,
            rebase_size);
    }
    if (bind_size)
    {
        memcpy(
            bytes + bind_offset,
            bind_bytes,
            bind_size);
    }
    u64 image_base = UINT64_C(0x100000000);
    u64 text_vm_address = image_base;
    u64 data_vm_address =
        image_base + data_file_offset;
    u64 string_cursor =
        string_table_offset + 1;
    u32 string_index = 1;
    bool valid = true;
    for (u32 symbol_index = 0;
        symbol_index < object->symbol_count;
        symbol_index += 1)
    {
        u32 import_index =
            import_indices[symbol_index];
        if (import_index == UINT32_MAX)
        {
            continue;
        }
        ObjectSymbol* symbol =
            &object->symbols[symbol_index];
        u64 nlist =
            symbol_table_offset +
            (u64)import_index * MACH_NLIST_SIZE;
        link_write_u32(bytes, nlist, string_index);
        bytes[nlist + 4] = 0x01;
        link_write_u16(bytes, nlist + 6, 0x0100);
        bytes[string_cursor++] = '_';
        memcpy(
            bytes + string_cursor,
            symbol->name.pointer,
            symbol->name.length);
        string_cursor += symbol->name.length + 1;
        string_index +=
            (u32)symbol->name.length + 2;
        u64 slot_address =
            data_vm_address +
            (got_offset - data_file_offset) +
            (u64)import_index * sizeof(u64);
        u64 current_stub_offset =
            stub_offset +
            (u64)import_index * stub_size;
        u64 stub_address =
            text_vm_address + current_stub_offset;
        if (object->target.cpu_arch ==
            CPU_ARCH_X86_64)
        {
            s64 displacement =
                (s64)slot_address -
                (s64)(stub_address + 6);
            if (displacement < INT32_MIN ||
                displacement > INT32_MAX)
            {
                valid = false;
                break;
            }
            bytes[current_stub_offset] = 0xff;
            bytes[current_stub_offset + 1] = 0x25;
            link_write_u32(
                bytes,
                current_stub_offset + 2,
                (u32)(s32)displacement);
        }
        else
        {
            u64 page_offset =
                slot_address & 0xfff;
            if (page_offset % 8)
            {
                valid = false;
                break;
            }
            link_write_u32(
                bytes,
                current_stub_offset,
                link_aarch64_adrp(
                    16,
                    stub_address,
                    slot_address,
                    &valid));
            link_write_u32(
                bytes,
                current_stub_offset + 4,
                0xf9400000 |
                    ((u32)(page_offset / 8) << 10) |
                    (16 << 5) |
                    17);
            link_write_u32(
                bytes,
                current_stub_offset + 8,
                0xd61f0220);
            link_write_u32(
                bytes,
                current_stub_offset + 12,
                0xd503201f);
        }
    }
    if (!valid)
    {
        result.error = LINK_ERROR_RELOCATION;
        return result;
    }
    for (u32 index = 0;
        index < object->relocation_count;
        index += 1)
    {
        ObjectRelocation* relocation =
            &object->relocations[index];
        if (relocation->section >=
                OBJECT_SECTION_COUNT ||
            relocation->symbol >=
                object->symbol_count)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSection* section =
            &object->sections[relocation->section];
        u64 width =
            relocation->kind ==
                OBJECT_RELOCATION_ABSOLUTE64 ?
                8 : 4;
        if (relocation->offset >
                section->data.length ||
            width >
                section->data.length -
                    relocation->offset)
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
        ObjectSymbol* symbol =
            &object->symbols[relocation->symbol];
        u64 symbol_address = 0;
        if (symbol->section ==
            OBJECT_SECTION_UNDEFINED)
        {
            u32 import_index =
                import_indices[relocation->symbol];
            if (import_index == UINT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                result.symbol = symbol->name;
                return result;
            }
            symbol_address =
                text_vm_address + stub_offset +
                (u64)import_index * stub_size;
        }
        else if (symbol->section <
            OBJECT_SECTION_COUNT)
        {
            symbol_address =
                image_base +
                section_offsets[symbol->section] +
                symbol->value;
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            result.symbol = symbol->name;
            return result;
        }
        u64 output_offset =
            section_offsets[relocation->section] +
            relocation->offset;
        u64 place_address =
            image_base + output_offset;
        if (relocation->kind ==
            OBJECT_RELOCATION_X86_64_PC32)
        {
            s64 value =
                (s64)symbol_address +
                relocation->addend -
                (s64)place_address;
            if (value < INT32_MIN ||
                value > INT32_MAX)
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                output_offset,
                (u32)(s32)value);
        }
        else if (relocation->kind ==
            OBJECT_RELOCATION_AARCH64_CALL26)
        {
            s64 displacement =
                (s64)symbol_address +
                relocation->addend -
                (s64)place_address;
            s64 words = displacement / 4;
            if (displacement % 4 ||
                words < -(1 << 25) ||
                words >= (1 << 25))
            {
                result.error = LINK_ERROR_RELOCATION;
                return result;
            }
            link_write_u32(
                bytes,
                output_offset,
                0x94000000 |
                    ((u32)words & 0x03ffffff));
        }
        else if (relocation->kind ==
            OBJECT_RELOCATION_ABSOLUTE64)
        {
            link_write_u64(
                bytes,
                output_offset,
                symbol_address +
                    (u64)relocation->addend);
        }
        else
        {
            result.error = LINK_ERROR_RELOCATION;
            return result;
        }
    }
    link_write_u32(bytes, 0, 0xfeedfacf);
    link_write_u32(
        bytes,
        4,
        object->target.cpu_arch ==
                CPU_ARCH_X86_64 ?
            0x01000007 :
            0x0100000c);
    link_write_u32(
        bytes,
        8,
        object->target.cpu_arch ==
                CPU_ARCH_X86_64 ?
            3 : 0);
    link_write_u32(bytes, 12, 2);
    link_write_u32(bytes, 16, MACH_COMMAND_COUNT);
    link_write_u32(bytes, 20, (u32)command_size);
    link_write_u32(bytes, 24, 0x200084);
    u64 command = MACH_HEADER_SIZE;
#define BUSTER_LINK_MACH_SEGMENT( \
        name, vm_address, vm_size, file_offset, \
        file_segment_size, maximum, initial) \
    do \
    { \
        link_write_u32(bytes, command, 0x19); \
        link_write_u32( \
            bytes, command + 4, \
            MACH_SEGMENT_COMMAND_SIZE); \
        link_mach_name_write(bytes, command + 8, (name)); \
        link_write_u64(bytes, command + 24, (vm_address)); \
        link_write_u64(bytes, command + 32, (vm_size)); \
        link_write_u64(bytes, command + 40, (file_offset)); \
        link_write_u64( \
            bytes, command + 48, (file_segment_size)); \
        link_write_u32(bytes, command + 56, (maximum)); \
        link_write_u32(bytes, command + 60, (initial)); \
        command += MACH_SEGMENT_COMMAND_SIZE; \
    } while (0)
    BUSTER_LINK_MACH_SEGMENT(
        "__PAGEZERO",
        0,
        image_base,
        0,
        0,
        0,
        0);
    link_write_u32(bytes, command, 0x19);
    link_write_u32(
        bytes,
        command + 4,
        MACH_TEXT_COMMAND_SIZE);
    link_mach_name_write(
        bytes,
        command + 8,
        "__TEXT");
    link_write_u64(bytes, command + 24, image_base);
    link_write_u64(
        bytes,
        command + 32,
        data_file_offset);
    link_write_u64(
        bytes,
        command + 48,
        data_file_offset);
    link_write_u32(bytes, command + 56, 5);
    link_write_u32(bytes, command + 60, 5);
    link_write_u32(bytes, command + 64, 1);
    link_mach_section_write(
        bytes,
        command + MACH_SEGMENT_COMMAND_SIZE,
        "__text",
        "__TEXT",
        image_base +
            section_offsets[OBJECT_SECTION_TEXT],
        text_end -
            section_offsets[OBJECT_SECTION_TEXT],
        (u32)section_offsets[OBJECT_SECTION_TEXT],
        4,
        0x80000400,
        0,
        0);
    command += MACH_TEXT_COMMAND_SIZE;
    link_write_u32(bytes, command, 0x19);
    link_write_u32(
        bytes,
        command + 4,
        MACH_DATA_COMMAND_SIZE);
    link_mach_name_write(
        bytes,
        command + 8,
        "__DATA");
    link_write_u64(
        bytes,
        command + 24,
        data_vm_address);
    link_write_u64(
        bytes,
        command + 32,
        linkedit_offset - data_file_offset);
    link_write_u64(
        bytes,
        command + 40,
        data_file_offset);
    link_write_u64(
        bytes,
        command + 48,
        linkedit_offset - data_file_offset);
    link_write_u32(bytes, command + 56, 3);
    link_write_u32(bytes, command + 60, 3);
    link_write_u32(bytes, command + 64, 2);
    link_mach_section_write(
        bytes,
        command + MACH_SEGMENT_COMMAND_SIZE,
        "__data",
        "__DATA",
        data_vm_address,
        object->sections[
            OBJECT_SECTION_DATA].data.length,
        (u32)data_file_offset,
        3,
        0,
        0,
        0);
    link_mach_section_write(
        bytes,
        command + MACH_SEGMENT_COMMAND_SIZE +
            MACH_SECTION_SIZE,
        "__got",
        "__DATA",
        image_base + got_offset,
        (u64)import_count * sizeof(u64),
        (u32)got_offset,
        3,
        0,
        0,
        0);
    command += MACH_DATA_COMMAND_SIZE;
    BUSTER_LINK_MACH_SEGMENT(
        "__LINKEDIT",
        image_base + linkedit_offset,
        align_forward(
            file_size - linkedit_offset,
            MACH_PAGE_SIZE),
        linkedit_offset,
        file_size - linkedit_offset,
        1,
        1);
#undef BUSTER_LINK_MACH_SEGMENT
    link_write_u32(bytes, command, 0x80000022);
    link_write_u32(
        bytes,
        command + 4,
        MACH_DYLD_INFO_COMMAND_SIZE);
    link_write_u32(
        bytes,
        command + 8,
        rebase_count ? (u32)linkedit_offset : 0);
    link_write_u32(
        bytes,
        command + 12,
        (u32)rebase_size);
    link_write_u32(
        bytes,
        command + 16,
        import_count ? (u32)bind_offset : 0);
    link_write_u32(
        bytes,
        command + 20,
        (u32)bind_size);
    command += MACH_DYLD_INFO_COMMAND_SIZE;
    link_write_u32(bytes, command, 0xe);
    link_write_u32(
        bytes,
        command + 4,
        MACH_DYLINKER_COMMAND_SIZE);
    link_write_u32(bytes, command + 8, 12);
    memcpy(
        bytes + command + 12,
        dyld_path,
        sizeof(dyld_path));
    command += MACH_DYLINKER_COMMAND_SIZE;
    link_write_u32(bytes, command, 0xc);
    link_write_u32(
        bytes,
        command + 4,
        MACH_DYLIB_COMMAND_SIZE);
    link_write_u32(bytes, command + 8, 24);
    link_write_u32(bytes, command + 16, 0x10000);
    link_write_u32(bytes, command + 20, 0x10000);
    memcpy(
        bytes + command + 24,
        system_path,
        sizeof(system_path));
    command += MACH_DYLIB_COMMAND_SIZE;
    ObjectSymbol* entry_symbol =
        &object->symbols[entry_symbol_index];
    u64 entry_offset =
        section_offsets[entry_symbol->section] +
        entry_symbol->value;
    link_write_u32(bytes, command, 0x80000028);
    link_write_u32(
        bytes,
        command + 4,
        MACH_MAIN_COMMAND_SIZE);
    link_write_u64(bytes, command + 8, entry_offset);
    command += MACH_MAIN_COMMAND_SIZE;
    link_write_u32(bytes, command, 0x32);
    link_write_u32(
        bytes,
        command + 4,
        MACH_BUILD_VERSION_COMMAND_SIZE);
    link_write_u32(
        bytes,
        command + 8,
        object->target.os == OPERATING_SYSTEM_IOS ?
#if BUSTER_IOS_SIMULATOR
            7 :
#else
            2 : 1);
#endif
#if BUSTER_IOS_SIMULATOR
            1);
#endif
    link_write_u32(bytes, command + 12, 0x000d0000);
    link_write_u32(bytes, command + 16, 0x000d0000);
    command += MACH_BUILD_VERSION_COMMAND_SIZE;
    link_write_u32(bytes, command, 0x2);
    link_write_u32(
        bytes,
        command + 4,
        MACH_SYMTAB_COMMAND_SIZE);
    link_write_u32(
        bytes,
        command + 8,
        (u32)symbol_table_offset);
    link_write_u32(bytes, command + 12, import_count);
    link_write_u32(
        bytes,
        command + 16,
        (u32)string_table_offset);
    link_write_u32(
        bytes,
        command + 20,
        (u32)string_table_size);
    command += MACH_SYMTAB_COMMAND_SIZE;
    link_write_u32(bytes, command, 0xb);
    link_write_u32(
        bytes,
        command + 4,
        MACH_DYSYMTAB_COMMAND_SIZE);
    link_write_u32(bytes, command + 24, 0);
    link_write_u32(bytes, command + 28, import_count);
    command += MACH_DYSYMTAB_COMMAND_SIZE;
    link_write_u32(bytes, command, 0x1d);
    link_write_u32(
        bytes,
        command + 4,
        MACH_CODE_SIGNATURE_COMMAND_SIZE);
    link_write_u32(
        bytes,
        command + 8,
        (u32)signature_offset);
    link_write_u32(
        bytes,
        command + 12,
        (u32)signature_size);
    command += MACH_CODE_SIGNATURE_COMMAND_SIZE;
    if (command !=
        header_end)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    u64 code_directory = signature_offset + 20;
    link_write_u32_be(
        bytes,
        signature_offset,
        0xfade0cc0);
    link_write_u32_be(
        bytes,
        signature_offset + 4,
        (u32)signature_size);
    link_write_u32_be(
        bytes,
        signature_offset + 8,
        1);
    link_write_u32_be(
        bytes,
        signature_offset + 12,
        0);
    link_write_u32_be(
        bytes,
        signature_offset + 16,
        20);
    link_write_u32_be(
        bytes,
        code_directory,
        0xfade0c02);
    link_write_u32_be(
        bytes,
        code_directory + 4,
        (u32)code_directory_size);
    link_write_u32_be(
        bytes,
        code_directory + 8,
        0x20100);
    link_write_u32_be(
        bytes,
        code_directory + 12,
        2);
    link_write_u32_be(
        bytes,
        code_directory + 16,
        MACH_CODE_DIRECTORY_HEADER_SIZE +
            MACH_CODE_DIRECTORY_IDENTIFIER_SIZE);
    link_write_u32_be(
        bytes,
        code_directory + 20,
        MACH_CODE_DIRECTORY_HEADER_SIZE);
    link_write_u32_be(
        bytes,
        code_directory + 28,
        (u32)code_slot_count);
    link_write_u32_be(
        bytes,
        code_directory + 32,
        (u32)signature_offset);
    bytes[code_directory + 36] = 32;
    bytes[code_directory + 37] = 2;
    bytes[code_directory + 39] = 12;
    memcpy(
        bytes + code_directory +
            MACH_CODE_DIRECTORY_HEADER_SIZE,
        "buster",
        MACH_CODE_DIRECTORY_IDENTIFIER_SIZE);
    u64 hash_offset =
        code_directory +
        MACH_CODE_DIRECTORY_HEADER_SIZE +
        MACH_CODE_DIRECTORY_IDENTIFIER_SIZE;
    for (u64 slot = 0;
        slot < code_slot_count;
        slot += 1)
    {
        u64 page_offset =
            slot * MACH_CODE_PAGE_SIZE;
        u64 page_size = BUSTER_MIN(
            (u64)MACH_CODE_PAGE_SIZE,
            signature_offset - page_offset);
        link_sha256(
            bytes + page_offset,
            page_size,
            bytes + hash_offset + slot * 32);
    }
    if (options.output_path.length &&
        !link_write_executable_file(
            options.output_path,
            result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL NativeExecutableLinkResult
link_native_executable_android_elf64(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
{
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_PROGRAM_HEADER_SIZE = 56,
        ELF_DYNAMIC_SIZE = 16,
        ELF_DYNAMIC_COUNT = 12,
    };
    static char8 const interpreter[] =
        "/system/bin/linker64";
    static char8 const library[] = "libc.so";
    NativeExecutableLinkOptions staging_options =
        options;
    staging_options.output_path = (String8){0};
    ObjectFile staging_object = *object;
    staging_object.target.os = OPERATING_SYSTEM_LINUX;
    bool has_import = false;
    for (u32 index = 0;
        index < object->symbol_count;
        index += 1)
    {
        if (object->symbols[index].section ==
            OBJECT_SECTION_UNDEFINED)
        {
            has_import = true;
            break;
        }
    }
    NativeExecutableLinkResult result = {0};
    if (object->target.cpu_arch ==
        CPU_ARCH_X86_64)
    {
        result = has_import ?
            link_native_executable_elf64_x86_64_dynamic(
                arena,
                &staging_object,
                staging_options) :
            link_native_executable_elf64_x86_64(
                arena,
                &staging_object,
                staging_options);
    }
    else if (object->target.cpu_arch ==
        CPU_ARCH_AARCH64)
    {
        result = has_import ?
            link_native_executable_elf64_aarch64_dynamic(
                arena,
                &staging_object,
                staging_options) :
            link_native_executable_elf64_aarch64(
                arena,
                &staging_object,
                staging_options);
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
        u64 interpreter_program_header =
            ELF_HEADER_SIZE +
            ELF_PROGRAM_HEADER_SIZE;
        u64 interpreter_offset =
            link_read_u64(
                bytes,
                interpreter_program_header + 8);
        u64 interpreter_size =
            link_read_u64(
                bytes,
                interpreter_program_header + 32);
        if (sizeof(interpreter) >
            interpreter_size)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        memset(
            bytes + interpreter_offset,
            0,
            interpreter_size);
        memcpy(
            bytes + interpreter_offset,
            interpreter,
            sizeof(interpreter));
        u64 dynamic_program_header =
            ELF_HEADER_SIZE +
            4 * ELF_PROGRAM_HEADER_SIZE;
        u64 dynamic_offset =
            link_read_u64(
                bytes,
                dynamic_program_header + 8);
        u64 string_table_offset = 0;
        for (u32 index = 0;
            index < ELF_DYNAMIC_COUNT;
            index += 1)
        {
            u64 entry =
                dynamic_offset +
                (u64)index * ELF_DYNAMIC_SIZE;
            if (link_read_u64(bytes, entry) == 5)
            {
                string_table_offset =
                    link_read_u64(bytes, entry + 8) -
                    UINT64_C(0x400000);
                break;
            }
        }
        if (!string_table_offset)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        memset(
            bytes + string_table_offset + 1,
            0,
            sizeof("libc.so.6"));
        memcpy(
            bytes + string_table_offset + 1,
            library,
            sizeof(library));
    }
    if (options.output_path.length &&
        !link_write_executable_file(
            options.output_path,
            result.executable))
    {
        result.error = LINK_ERROR_FILE_WRITE;
    }
    return result;
}

NativeExecutableLinkResult link_native_executable(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options)
{
    NativeExecutableLinkResult result = {0};
    if (!arena || !object ||
        object->error != OBJECT_ERROR_NONE)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    if (object->target.os ==
            OPERATING_SYSTEM_LINUX &&
        object->target.cpu_arch ==
            CPU_ARCH_X86_64)
    {
        for (u32 symbol_index = 0;
            symbol_index < object->symbol_count;
            symbol_index += 1)
        {
            if (object->symbols[symbol_index].section ==
                OBJECT_SECTION_UNDEFINED)
            {
                return
                    link_native_executable_elf64_x86_64_dynamic(
                        arena,
                        object,
                        options);
            }
        }
        return
            link_native_executable_elf64_x86_64(
                arena,
                object,
                options);
    }
    if (object->target.os ==
            OPERATING_SYSTEM_WINDOWS &&
        (object->target.cpu_arch ==
                CPU_ARCH_X86_64 ||
            object->target.cpu_arch ==
                CPU_ARCH_AARCH64))
    {
        return link_native_executable_pe64(
            arena,
            object,
            options);
    }
    if ((object->target.os ==
                OPERATING_SYSTEM_MACOS ||
            object->target.os ==
                OPERATING_SYSTEM_IOS) &&
        (object->target.cpu_arch ==
                CPU_ARCH_X86_64 ||
            object->target.cpu_arch ==
                CPU_ARCH_AARCH64))
    {
        return link_native_executable_mach_o64(
            arena,
            object,
            options);
    }
    if (object->target.os ==
            OPERATING_SYSTEM_ANDROID &&
        (object->target.cpu_arch ==
                CPU_ARCH_X86_64 ||
            object->target.cpu_arch ==
                CPU_ARCH_AARCH64))
    {
        return link_native_executable_android_elf64(
            arena,
            object,
            options);
    }
    if (object->target.os ==
            OPERATING_SYSTEM_LINUX &&
        object->target.cpu_arch ==
            CPU_ARCH_AARCH64)
    {
        for (u32 symbol_index = 0;
            symbol_index < object->symbol_count;
            symbol_index += 1)
        {
            if (object->symbols[symbol_index].section ==
                OBJECT_SECTION_UNDEFINED)
            {
                return
                    link_native_executable_elf64_aarch64_dynamic(
                        arena,
                        object,
                        options);
            }
        }
        return link_native_executable_elf64_aarch64(
            arena,
            object,
            options);
    }
    result.error = LINK_ERROR_UNSUPPORTED_HOST;
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL String8
link_test_temporary_executable_path(
    Arena* arena,
    String8 name,
    String8 suffix)
{
#if BUSTER_ANDROID || BUSTER_IOS
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(name);
    BUSTER_UNUSED(suffix);
    return (String8){0};
#else
    return buster_test_temporary_path(
        arena,
        name,
        suffix);
#endif
}

BUSTER_GLOBAL_LOCAL ObjectFile link_test_object_make(
    Arena* arena,
    Target target,
    ByteSlice text,
    ObjectSymbol* symbols,
    u32 symbol_count,
    ObjectRelocation* relocations,
    u32 relocation_count)
{
    ObjectSection* sections = arena_allocate(
        arena,
        ObjectSection,
        OBJECT_SECTION_COUNT);
    sections[OBJECT_SECTION_TEXT] =
        (ObjectSection){
            .name = S8(".text"),
            .data = text,
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 16,
        };
    sections[OBJECT_SECTION_READ_ONLY_DATA] =
        (ObjectSection){
            .name = S8(".rodata"),
            .kind =
                OBJECT_SECTION_READ_ONLY_DATA,
            .alignment = 8,
        };
    sections[OBJECT_SECTION_DATA] =
        (ObjectSection){
            .name = S8(".data"),
            .kind = OBJECT_SECTION_DATA,
            .alignment = 8,
        };
    return (ObjectFile){
        .sections = sections,
        .symbols = symbols,
        .relocations = relocations,
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
        .symbol_count = symbol_count,
        .relocation_count = relocation_count,
    };
}

UnitTestResult link_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    static u8 const sha256_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    u8 sha256_result[32] = {0};
    link_sha256(
        (u8 const*)"abc",
        3,
        sha256_result);
    BUSTER_TEST(
        arguments,
        memcmp(
            sha256_result,
            sha256_abc,
            sizeof(sha256_abc)) == 0);
    Target target = target_native;
#if BUSTER_CPU_ARCH_X86_64
    u8 answer_text[] = {
        0xb8, 42, 0, 0, 0,
        0xc3,
    };
    u8 main_text[] = {
        0xe8, 0, 0, 0, 0,
        0x83, 0xe8, 42,
        0xc3,
    };
    ByteSlice answer_bytes =
        (ByteSlice)
            BUSTER_ARRAY_TO_SLICE(answer_text);
    ByteSlice main_bytes =
        (ByteSlice)
            BUSTER_ARRAY_TO_SLICE(main_text);
    ObjectRelocation main_relocation = {
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind =
            OBJECT_RELOCATION_X86_64_PC32,
    };
#elif BUSTER_CPU_ARCH_AARCH64
    u32 answer_instructions[] = {
        0x52800540,
        0xd65f03c0,
    };
    u32 main_instructions[] = {
        0xa9bf7bfd,
        0x910003fd,
        0x94000000,
        0x5100a800,
        0xa8c17bfd,
        0xd65f03c0,
    };
    ByteSlice answer_bytes = {
        .pointer = (u8*)answer_instructions,
        .length = sizeof(answer_instructions),
    };
    ByteSlice main_bytes = {
        .pointer = (u8*)main_instructions,
        .length = sizeof(main_instructions),
    };
    ObjectRelocation main_relocation = {
        .offset = 8,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind =
            OBJECT_RELOCATION_AARCH64_CALL26,
    };
#endif
#if BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64
    ObjectSymbol answer_symbols[] = {
        {
            .name = S8("answer"),
            .size = answer_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectSymbol main_symbols[] = {
        {
            .name = S8("main"),
            .size = main_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("answer"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectFile objects[] = {
        link_test_object_make(
            arguments->arena,
            target,
            answer_bytes,
            answer_symbols,
            BUSTER_ARRAY_LENGTH(answer_symbols),
            0,
            0),
        link_test_object_make(
            arguments->arena,
            target,
            main_bytes,
            main_symbols,
            BUSTER_ARRAY_LENGTH(main_symbols),
            &main_relocation,
            1),
    };
    LinkObjectResult linked = link_objects(
        arguments->arena,
        objects,
        BUSTER_ARRAY_LENGTH(objects),
        (LinkOptions){0});
    BUSTER_TEST(arguments,
        linked.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments,
        linked.object.symbol_count == 2);
    BUSTER_TEST(arguments,
        linked.object.relocation_count == 1);
    bool linked_text_size_matches = false;
    if (linked.object.sections &&
        linked.object.section_count >
            OBJECT_SECTION_TEXT)
    {
        linked_text_size_matches =
            linked.object.sections[
                OBJECT_SECTION_TEXT].data.length ==
                    align_forward(
                        answer_bytes.length,
                        16) +
                    main_bytes.length;
    }
    BUSTER_TEST(arguments,
        linked_text_size_matches);
    ObjectFile duplicate_objects[] = {
        objects[0],
        objects[0],
    };
    LinkObjectResult duplicate = link_objects(
        arguments->arena,
        duplicate_objects,
        BUSTER_ARRAY_LENGTH(duplicate_objects),
        (LinkOptions){0});
    BUSTER_TEST(arguments,
        duplicate.error ==
            LINK_ERROR_DUPLICATE_SYMBOL);
    BUSTER_STRING_TEST(
        arguments,
        duplicate.symbol,
        S8("answer"));
    ObjectFile unresolved_object = objects[1];
    LinkObjectResult unresolved = link_objects(
        arguments->arena,
        &unresolved_object,
        1,
        (LinkOptions){0});
    BUSTER_TEST(arguments,
        unresolved.error ==
            LINK_ERROR_UNRESOLVED_SYMBOL);
    BUSTER_STRING_TEST(
        arguments,
        unresolved.symbol,
        S8("answer"));
    LinkObjectResult permitted = link_objects(
        arguments->arena,
        &unresolved_object,
        1,
        (LinkOptions){
            .allow_undefined_symbols = true,
        });
    BUSTER_TEST(arguments,
        permitted.error == LINK_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
    ObjectFile windows_object = linked.object;
    windows_object.target.os =
        OPERATING_SYSTEM_WINDOWS;
    String8 pe_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-link-test"),
            S8(".exe"));
    NativeExecutableLinkResult pe_executable =
        link_native_executable(
            arguments->arena,
            &windows_object,
            (NativeExecutableLinkOptions){
                .output_path = pe_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        pe_executable.error == LINK_ERROR_NONE);
    bool pe_header_valid =
        pe_executable.executable.length > 0x84 &&
        pe_executable.executable.pointer[0] == 'M' &&
        pe_executable.executable.pointer[1] == 'Z' &&
        memcmp(
            pe_executable.executable.pointer + 0x80,
            "PE\0\0",
            4) == 0 &&
        (pe_executable.executable.pointer[0x96] &
            0x01) != 0;
    BUSTER_TEST(arguments, pe_header_valid);
    u8 pe_libc_main_text[] = {
        0x48, 0x83, 0xec, 0x28,
        0xb9, 0xd6, 0xff, 0xff, 0xff,
        0xe8, 0, 0, 0, 0,
        0x83, 0xe8, 42,
        0x48, 0x83, 0xc4, 0x28,
        0xc3,
    };
    ObjectSymbol pe_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(pe_libc_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation pe_libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile pe_libc_object = link_test_object_make(
        arguments->arena,
        windows_object.target,
        (ByteSlice)
            BUSTER_ARRAY_TO_SLICE(
                pe_libc_main_text),
        pe_libc_symbols,
        BUSTER_ARRAY_LENGTH(pe_libc_symbols),
        &pe_libc_relocation,
        1);
    String8 pe_libc_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-libc-link-test"),
            S8(".exe"));
    NativeExecutableLinkResult pe_libc_executable =
        link_native_executable(
            arguments->arena,
            &pe_libc_object,
            (NativeExecutableLinkOptions){
                .output_path =
                    pe_libc_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        pe_libc_executable.error ==
            LINK_ERROR_NONE);
#endif
    u32 aarch64_main_instructions[] = {
        0x52800000,
        0xd65f03c0,
    };
    ObjectSymbol aarch64_symbols[] = {
        {
            .name = S8("main"),
            .size =
                sizeof(aarch64_main_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    Target aarch64_target = target;
    aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
    aarch64_target.os = OPERATING_SYSTEM_LINUX;
    ObjectFile aarch64_object = link_test_object_make(
        arguments->arena,
        aarch64_target,
        (ByteSlice){
            .pointer =
                (u8*)aarch64_main_instructions,
            .length =
                sizeof(aarch64_main_instructions),
        },
        aarch64_symbols,
        BUSTER_ARRAY_LENGTH(aarch64_symbols),
        0,
        0);
    String8 aarch64_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-aarch64-link-test"),
            S8(""));
    NativeExecutableLinkResult aarch64_executable =
        link_native_executable(
            arguments->arena,
            &aarch64_object,
            (NativeExecutableLinkOptions){
                .output_path =
                    aarch64_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        aarch64_executable.error ==
            LINK_ERROR_NONE);
    bool aarch64_header_valid =
        aarch64_executable.executable.length > 20 &&
        aarch64_executable.executable.pointer[0] ==
            0x7f &&
        aarch64_executable.executable.pointer[1] ==
            'E' &&
        aarch64_executable.executable.pointer[18] ==
            183;
    BUSTER_TEST(arguments, aarch64_header_valid);
    u32 aarch64_libc_instructions[] = {
        0xa9bf7bfd,
        0x910003fd,
        0x52800540,
        0x94000000,
        0x5100a800,
        0xa8c17bfd,
        0xd65f03c0,
    };
    ObjectSymbol aarch64_libc_symbols[] = {
        {
            .name = S8("main"),
            .size =
                sizeof(aarch64_libc_instructions),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation aarch64_libc_relocation = {
        .offset = 3 * sizeof(u32),
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_AARCH64_CALL26,
    };
    ObjectFile aarch64_libc_object =
        link_test_object_make(
            arguments->arena,
            aarch64_target,
            (ByteSlice){
                .pointer =
                    (u8*)aarch64_libc_instructions,
                .length = sizeof(
                    aarch64_libc_instructions),
            },
            aarch64_libc_symbols,
            BUSTER_ARRAY_LENGTH(
                aarch64_libc_symbols),
            &aarch64_libc_relocation,
            1);
    String8 aarch64_libc_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-aarch64-libc-link-test"),
            S8(""));
    NativeExecutableLinkResult
        aarch64_libc_executable =
            link_native_executable(
                arguments->arena,
                &aarch64_libc_object,
                (NativeExecutableLinkOptions){
                    .output_path =
                        aarch64_libc_output_path,
                    .entry_symbol = S8("main"),
                });
    BUSTER_TEST(arguments,
        aarch64_libc_executable.error ==
            LINK_ERROR_NONE);
    ObjectFile aarch64_pe_object =
        aarch64_object;
    aarch64_pe_object.target.os =
        OPERATING_SYSTEM_WINDOWS;
    String8 aarch64_pe_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-aarch64-pe-test"),
            S8(".exe"));
    NativeExecutableLinkResult aarch64_pe_executable =
        link_native_executable(
            arguments->arena,
            &aarch64_pe_object,
            (NativeExecutableLinkOptions){
                .output_path =
                    aarch64_pe_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        aarch64_pe_executable.error ==
            LINK_ERROR_NONE);
    bool aarch64_pe_header_valid =
        aarch64_pe_executable.executable.length >
            0x88 &&
        aarch64_pe_executable.executable.pointer[
            0x84] == 0x64 &&
        aarch64_pe_executable.executable.pointer[
            0x85] == 0xaa;
    BUSTER_TEST(
        arguments,
        aarch64_pe_header_valid);
    ObjectFile aarch64_pe_libc_object =
        aarch64_libc_object;
    aarch64_pe_libc_object.target.os =
        OPERATING_SYSTEM_WINDOWS;
    NativeExecutableLinkResult
        aarch64_pe_libc_executable =
            link_native_executable(
                arguments->arena,
                &aarch64_pe_libc_object,
                (NativeExecutableLinkOptions){
                    .entry_symbol = S8("main"),
                });
    BUSTER_TEST(arguments,
        aarch64_pe_libc_executable.error ==
            LINK_ERROR_NONE);
    ObjectFile android_object =
        aarch64_libc_object;
    android_object.target.os =
        OPERATING_SYSTEM_ANDROID;
    String8 android_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-android-test"),
            S8(""));
    NativeExecutableLinkResult android_executable =
        link_native_executable(
            arguments->arena,
            &android_object,
            (NativeExecutableLinkOptions){
                .output_path =
                    android_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        android_executable.error ==
            LINK_ERROR_NONE);
    bool android_header_valid =
        android_executable.executable.length > 18 &&
        android_executable.executable.pointer[16] ==
            3 &&
        android_executable.executable.pointer[17] ==
            0;
    BUSTER_TEST(arguments, android_header_valid);
    ObjectFile aarch64_mach_object =
        aarch64_object;
    aarch64_mach_object.target.os =
        OPERATING_SYSTEM_MACOS;
    String8 aarch64_mach_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-aarch64-macho-test"),
            S8(""));
    NativeExecutableLinkResult
        aarch64_mach_executable =
            link_native_executable(
                arguments->arena,
                &aarch64_mach_object,
                (NativeExecutableLinkOptions){
                    .output_path =
                        aarch64_mach_output_path,
                    .entry_symbol = S8("main"),
                });
    BUSTER_TEST(arguments,
        aarch64_mach_executable.error ==
            LINK_ERROR_NONE);
    bool aarch64_mach_header_valid =
        aarch64_mach_executable.executable.length >
            32 &&
        aarch64_mach_executable.executable.pointer[0] ==
            0xcf &&
        aarch64_mach_executable.executable.pointer[1] ==
            0xfa &&
        aarch64_mach_executable.executable.pointer[2] ==
            0xed &&
        aarch64_mach_executable.executable.pointer[3] ==
            0xfe &&
        (aarch64_mach_executable.executable.pointer[26] &
            0x20) != 0;
    BUSTER_TEST(
        arguments,
        aarch64_mach_header_valid);
    ObjectFile aarch64_mach_libc_object =
        aarch64_libc_object;
    aarch64_mach_libc_object.target.os =
        OPERATING_SYSTEM_MACOS;
    String8 aarch64_mach_libc_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-aarch64-macho-libc-test"),
            S8(""));
    NativeExecutableLinkResult
        aarch64_mach_libc_executable =
            link_native_executable(
                arguments->arena,
                &aarch64_mach_libc_object,
                (NativeExecutableLinkOptions){
                    .output_path =
                        aarch64_mach_libc_output_path,
                    .entry_symbol = S8("main"),
                });
    BUSTER_TEST(arguments,
        aarch64_mach_libc_executable.error ==
            LINK_ERROR_NONE);
    ObjectFile ios_mach_object =
        aarch64_mach_object;
    ios_mach_object.target.os =
        OPERATING_SYSTEM_IOS;
    String8 ios_mach_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-ios-macho-test"),
            S8(""));
    NativeExecutableLinkResult ios_mach_executable =
        link_native_executable(
            arguments->arena,
            &ios_mach_object,
            (NativeExecutableLinkOptions){
                .output_path =
                    ios_mach_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        ios_mach_executable.error ==
            LINK_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64
    ObjectFile x86_mach_object = linked.object;
    x86_mach_object.target.os =
        OPERATING_SYSTEM_MACOS;
    String8 x86_mach_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-x86-macho-test"),
            S8(""));
    NativeExecutableLinkResult x86_mach_executable =
        link_native_executable(
            arguments->arena,
            &x86_mach_object,
            (NativeExecutableLinkOptions){
                .output_path =
                    x86_mach_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        x86_mach_executable.error ==
            LINK_ERROR_NONE);
    u8 x86_mach_libc_text[] = {
        0x48, 0x83, 0xec, 0x08,
        0xbf, 0xd6, 0xff, 0xff, 0xff,
        0xe8, 0, 0, 0, 0,
        0x83, 0xe8, 42,
        0x48, 0x83, 0xc4, 0x08,
        0xc3,
    };
    ObjectSymbol x86_mach_libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(x86_mach_libc_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation x86_mach_libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile x86_mach_libc_object =
        link_test_object_make(
            arguments->arena,
            x86_mach_object.target,
            (ByteSlice)
                BUSTER_ARRAY_TO_SLICE(
                    x86_mach_libc_text),
            x86_mach_libc_symbols,
            BUSTER_ARRAY_LENGTH(
                x86_mach_libc_symbols),
            &x86_mach_libc_relocation,
            1);
    String8 x86_mach_libc_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-x86-macho-libc-test"),
            S8(""));
    NativeExecutableLinkResult
        x86_mach_libc_executable =
            link_native_executable(
                arguments->arena,
                &x86_mach_libc_object,
                (NativeExecutableLinkOptions){
                    .output_path =
                        x86_mach_libc_output_path,
                    .entry_symbol = S8("main"),
                });
    BUSTER_TEST(arguments,
        x86_mach_libc_executable.error ==
            LINK_ERROR_NONE);
#endif
#if BUSTER_MACOS
    String8* native_mach_paths = 0;
    u32 native_mach_path_count = 0;
#if BUSTER_CPU_ARCH_X86_64
    String8 x86_native_mach_paths[] = {
        x86_mach_output_path,
        x86_mach_libc_output_path,
    };
    native_mach_paths = x86_native_mach_paths;
    native_mach_path_count =
        BUSTER_ARRAY_LENGTH(x86_native_mach_paths);
#elif BUSTER_CPU_ARCH_AARCH64
    String8 aarch64_native_mach_paths[] = {
        aarch64_mach_output_path,
        aarch64_mach_libc_output_path,
    };
    native_mach_paths =
        aarch64_native_mach_paths;
    native_mach_path_count =
        BUSTER_ARRAY_LENGTH(
            aarch64_native_mach_paths);
#endif
    for (u32 path_index = 0;
        path_index < native_mach_path_count;
        path_index += 1)
    {
        String8 run_arguments[] = {
            native_mach_paths[path_index],
        };
        ProcessSpawnResult spawn =
            os_process_spawn(
                (SliceString8)
                    BUSTER_ARRAY_TO_SLICE(
                        run_arguments),
                (SliceString8){0},
                (SliceString8){0},
                (ProcessSpawnOptions){
                    .use_process_environment =
                        true,
                });
        BUSTER_TEST(arguments,
            spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait =
                os_process_wait_sync(
                    arguments->arena,
                    spawn);
            BUSTER_TEST(arguments,
                wait.result ==
                    PROCESS_RESULT_SUCCESS);
        }
    }
#endif
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    String8 native_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-link-test"),
            S8(""));
    NativeExecutableLinkResult native_executable =
        link_native_executable(
            arguments->arena,
            &linked.object,
            (NativeExecutableLinkOptions){
                .output_path =
                    native_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        native_executable.error ==
            LINK_ERROR_NONE);
    BUSTER_TEST(arguments,
        native_executable.executable.length >= 4);
    if (native_executable.error ==
        LINK_ERROR_NONE)
    {
        String8 run_arguments[] = {
            native_output_path,
        };
        ProcessSpawnResult spawn =
            os_process_spawn(
                (SliceString8)
                    BUSTER_ARRAY_TO_SLICE(
                        run_arguments),
                (SliceString8){0},
                (SliceString8){0},
                (ProcessSpawnOptions){
                    .use_process_environment =
                        true,
                });
        BUSTER_TEST(arguments,
            spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait =
                os_process_wait_sync(
                    arguments->arena,
                    spawn);
            BUSTER_TEST(arguments,
                wait.result ==
                    PROCESS_RESULT_SUCCESS);
        }
    }
    NativeExecutableLinkResult
        unresolved_native =
            link_native_executable(
                arguments->arena,
                &permitted.object,
                (NativeExecutableLinkOptions){0});
    BUSTER_TEST(arguments,
        unresolved_native.error ==
            LINK_ERROR_NONE);
    u8 libc_main_text[] = {
        0x48, 0x83, 0xec, 0x08,
        0xbf, 0xd6, 0xff, 0xff, 0xff,
        0xe8, 0, 0, 0, 0,
        0x83, 0xe8, 42,
        0x48, 0x83, 0xc4, 0x08,
        0xc3,
    };
    ObjectSymbol libc_symbols[] = {
        {
            .name = S8("main"),
            .size = sizeof(libc_main_text),
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("abs"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectRelocation libc_relocation = {
        .addend = -4,
        .offset = 10,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind = OBJECT_RELOCATION_X86_64_PC32,
    };
    ObjectFile libc_object = link_test_object_make(
        arguments->arena,
        target,
        (ByteSlice)
            BUSTER_ARRAY_TO_SLICE(libc_main_text),
        libc_symbols,
        BUSTER_ARRAY_LENGTH(libc_symbols),
        &libc_relocation,
        1);
    String8 libc_output_path =
        link_test_temporary_executable_path(
            arguments->arena,
            S8("buster-native-libc-link-test"),
            S8(""));
    NativeExecutableLinkResult libc_executable =
        link_native_executable(
            arguments->arena,
            &libc_object,
            (NativeExecutableLinkOptions){
                .output_path = libc_output_path,
                .entry_symbol = S8("main"),
            });
    BUSTER_TEST(arguments,
        libc_executable.error ==
            LINK_ERROR_NONE);
    if (libc_executable.error ==
        LINK_ERROR_NONE)
    {
        String8 run_arguments[] = {
            libc_output_path,
        };
        ProcessSpawnResult spawn =
            os_process_spawn(
                (SliceString8)
                    BUSTER_ARRAY_TO_SLICE(
                        run_arguments),
                (SliceString8){0},
                (SliceString8){0},
                (ProcessSpawnOptions){
                    .use_process_environment =
                        true,
                });
        BUSTER_TEST(arguments,
            spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait =
                os_process_wait_sync(
                    arguments->arena,
                    spawn);
            BUSTER_TEST(arguments,
                wait.result ==
                    PROCESS_RESULT_SUCCESS);
        }
    }
#endif
#else
    BUSTER_UNUSED(target);
#endif
    return result;
}
#endif
