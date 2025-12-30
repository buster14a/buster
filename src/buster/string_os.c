#pragma once
#include <buster/string_os.h>
#include <buster/arena.h>

BUSTER_IMPL StringOsListIterator string_os_list_iterator_initialize(StringOsList list)
{
    return (StringOsListIterator) {
        .list = list,
    };
}

BUSTER_IMPL StringOs string_os_list_iterator_next(StringOsListIterator* iterator)
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

        result = string_os_from_pointer_length(original_pointer, length);
#else
        position += 1;
        result = string_os_from_pointer(current);
#endif
        iterator->position = position;
    }

    return result;
}

BUSTER_IMPL OsArgumentBuilder* string_os_list_builder_create(Arena* arena, StringOs s)
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
        argument_builder->argv = string_os_list_builder_append(argument_builder, s);
    }
    return argument_builder;
}

BUSTER_IMPL StringOsList string_os_list_builder_append(OsArgumentBuilder* builder, StringOs arg)
{
#if defined(_WIN32)
    let result = string_os_duplicate_arena(builder->arena, arg, true);
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

BUSTER_IMPL StringOsList string_os_list_builder_end(OsArgumentBuilder* restrict builder)
{
#if defined(_WIN32)
    *(CharOs*)((u8*)builder->arena + builder->arena->position - sizeof(CharOs)) = 0;
#else
    string_os_list_builder_append(builder, (StringOs){});
#endif
    return builder->argv;
}

BUSTER_IMPL StringOsList string_os_list_create_from(Arena* arena, StringOsSlice arguments)
{
#if defined(_WIN32)
    u64 allocation_length = 0;

    for (u64 i = 0; i < arguments.length; i += 1)
    {
        allocation_length += arguments.pointer[i].length + 1;
    }

    let allocation = arena_allocate(arena, CharOs, allocation_length);

    for (u64 source_i = 0, destination_i = 0; source_i < arguments.length; source_i += 1)
    {
        let source_argument = arguments.pointer[source_i];
        memcpy(&allocation[destination_i], source_argument.pointer, BUSTER_SLICE_SIZE(source_argument));
        destination_i += source_argument.length;
        allocation[destination_i] = ' ';
        destination_i += 1;
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

BUSTER_IMPL StringOsList string_os_list_duplicate_and_substitute_first_argument(Arena* arena, StringOsList old_arguments, StringOs new_first_argument, StringOsSlice extra_arguments)
{
#if defined(_WIN32)
    let space_index = raw_string16_first_code_point(old_arguments, ' ');
    let old_argument_length = string16_length(old_arguments);
    let first_argument_end = space_index == BUSTER_STRING_NO_MATCH ? old_argument_length : space_index;
    u64 extra_length = 0;
    for (u64 i = 0; i < extra_arguments.length; i += 1)
    {
        let extra_argument = extra_arguments.pointer[i];
        extra_length += extra_argument.length + 1;
    }

    extra_length -= extra_length != 0;

    let new_length = new_first_argument.length + (old_argument_length - first_argument_end + (extra_length == 0)) + extra_length;
    let new_arguments = arena_allocate(arena, CharOs, new_length + 1);

    let char_size = sizeof(new_first_argument.pointer[0]);
    u64 i = 0;
    let copy_length = new_first_argument.length;
    memcpy(new_arguments + i, new_first_argument.pointer, copy_length * char_size);
    i += copy_length;

    new_arguments[i] = ' ';
    i += 1;

    if (first_argument_end != old_argument_length)
    {
        copy_length = old_argument_length - first_argument_end;
        memcpy(new_arguments + i, old_arguments + first_argument_end, char_size * copy_length);
        i += copy_length;

        new_arguments[i] = ' ';
        i += 1;
    }

    for (u64 extra_i = 0; extra_i < extra_arguments.length; extra_i += 1)
    {
        let extra_argument = extra_arguments.pointer[extra_i];
        memcpy(new_arguments + i, extra_argument.pointer, char_size * extra_argument.length);
        i += extra_argument.length;

        new_arguments[i] = ' ';
        i += 1;
    }

    new_arguments[new_length] = 0;

    return new_arguments;
#else
    let it = string_os_list_iterator_initialize(old_arguments);
    StringOs arg;
    u64 old_argument_count = 0;
    while ((arg = string_os_list_iterator_next(&it)).pointer)
    {
        old_argument_count += 1;
    }

    let total_argument_count = old_argument_count + extra_arguments.length;
    let new_arguments = arena_allocate(arena, CharOs*, total_argument_count + 1);
    new_arguments[0] = new_first_argument.pointer;

    if (old_argument_count > 1)
    {
        memcpy(new_arguments + 1, old_arguments + 1, sizeof(new_arguments[0]) * old_argument_count - 1); 
    }

    for (u64 i = 0; i < extra_arguments.length; i += 1)
    {
        let incoming_argument = extra_arguments.pointer[i];
        new_arguments[old_argument_count + i] = incoming_argument.pointer;
    }

    new_arguments[total_argument_count] = 0;

    return new_arguments;
#endif
}
