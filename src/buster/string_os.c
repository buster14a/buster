#pragma once
#include <buster/string_os.h>

BUSTER_IMPL StringOsListIterator os_string_list_iterator_initialize(StringOsList list)
{
    return (StringOsListIterator) {
        .list = list,
    };
}

BUSTER_IMPL StringOs os_string_list_iterator_next(StringOsListIterator* iterator)
{
    StringOs result = {};
    let list = iterator->list;
    let original_position = iterator->position;
    let position = original_position;

    let current = list[position];
    if (current)
    {
#if defined(_WIN32)
        let original_pointer = &list[position];
        let pointer = original_pointer;
        if (*pointer == '"')
        {
            // TODO: handle escape
            let double_quote = raw_string16_first_code_point(pointer + 1, '"');
            if (double_quote == BUSTER_STRING_NO_MATCH)
            {
                return result;
            }

            position += double_quote + 1 + 1;
            pointer = &list[position];
        }

        let space = raw_string16_first_code_point(pointer, ' ');
        let is_space = space != BUSTER_STRING_NO_MATCH;
        space = is_space ? space : 0;
        position += space;
        position += is_space ? 0 : string16_length(pointer);
        let length = position - original_position;

        if (is_space)
        {
            while (list[position] == ' ')
            {
                position += 1;
            }
        }

        result = os_string_from_pointer_length(original_pointer, length);
#else
        position += 1;
        result = os_string_from_pointer(current);
#endif
        iterator->position = position;
    }

    return result;
}

BUSTER_IMPL OsArgumentBuilder* os_string_list_builder_create(Arena* arena, StringOs s)
{
    let position = arena->position;
    let argument_builder = arena_allocate(arena, OsArgumentBuilder, 1);
    if (argument_builder)
    {
        *argument_builder = (OsArgumentBuilder) {
            .argv = 0,
                .arena = arena,
                .arena_offset = position,
        };
        argument_builder->argv = os_string_list_builder_append(argument_builder, s);
    }
    return argument_builder;
}

BUSTER_IMPL StringOsList os_string_list_builder_append(OsArgumentBuilder* builder, StringOs arg)
{
#if defined(_WIN32)
    let result = arena_duplicate_os_string(builder->arena, arg, true);
    if (result.pointer)
    {
        result.pointer[arg.length] = ' ';
    }
    return result.pointer;
#else
    let result = arena_allocate(builder->arena, CharOs*, 1);
    if (result)
    {
        *result = (CharOs*)arg.pointer;
    }
    return result;
#endif
}

BUSTER_IMPL StringOsList os_string_list_builder_end(OsArgumentBuilder* restrict builder)
{
#if defined(_WIN32)
    *(CharOs*)((u8*)builder->arena + builder->arena->position - sizeof(CharOs)) = 0;
#else
    os_string_list_builder_append(builder, (StringOs){});
#endif
    return builder->argv;
}

BUSTER_IMPL StringOsList os_string_list_create(Arena* arena, StringOsSlice arguments)
{
#if defined(_WIN32)
    u64 allocation_length = arguments.length; // arguments.length - 1 + NULL code point
                                              //
    for (u64 i = 0; i < arguments.length; i += 1)
    {
        allocation_length += arguments.pointer[i].length;
    }

    let allocation = arena_allocate(arena, CharOs, allocation_length);

    for (u64 source_i = 0, destination_i = 0; source_i < arguments.length; source_i += 1)
    {
        let source_argument = arguments.pointer[source_i];
        memcpy(&allocation[destination_i], source_argument.pointer, BUSTER_SLICE_SIZE(source_argument));
        source_i += source_argument.length;
        allocation[destination_i] = ' ';
    }

    allocation[allocation_length - 1] = 0;

    return allocation;
#else
    let list = arena_allocate(arena, CharOs*, arguments.length + 1);

    for (u64 i = 0; i < arguments.length; i += 1)
    {
        list[i] = arguments.pointer[i].pointer;
    }

    list[arguments.length] = 0;

    return list;
#endif
}

