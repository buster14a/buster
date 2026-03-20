#pragma once

// Scrape LLVM X86 TableGen files and generate compact C tables for the selector.
//
// Usage:
//   ./scrape_llvm /path/to/llvm
//   ./scrape_llvm /path/to/llvm --generate
//   ./scrape_llvm /path/to/llvm --generate-output src/buster/x86_64_llvm.c

#include <buster/base.h>
#include <buster/system_headers.h>
#include <buster/arena.h>
#include <buster/string8.h>
#include <buster/file.h>
#include <buster/path.h>
#include <buster/entry_point.h>
#include <buster/arguments.h>

#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#if !defined(_WIN32)
#include <regex.h>
#endif

#if BUSTER_UNITY_BUILD
#include <buster/entry_point.c>
#include <buster/target.c>
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.c>
#endif
#include <buster/assertion.c>
#include <buster/memory.c>
#include <buster/string8.c>
#include <buster/os.c>
#include <buster/string.c>
#include <buster/arena.c>
#if defined(_WIN32)
#include <buster/string16.c>
#endif
#include <buster/integer.c>
#include <buster/file.c>
#include <buster/path.c>
#include <buster/arguments.c>
#if BUSTER_INCLUDE_TESTS
#include <buster/test.c>
#endif
#endif

#define SCRAPE_LLVM_MAX_DISCOVERED_FILES 256
#define SCRAPE_LLVM_MAX_INSTRUCTION_COUNT 32768
#define SCRAPE_LLVM_MAX_SCHEDULE_WRITE_COUNT 8192
#define SCRAPE_LLVM_MAX_INSTRUCTION_SCHEDULE_COUNT 32768
#define SCRAPE_LLVM_MAX_PROCESSOR_MODEL_COUNT 64
#define SCRAPE_LLVM_MAX_PROCESSOR_RESOURCE_COUNT 8192
#define SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT 2048
#define SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE 16
#define SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT_PER_SCHEDULE 4
#define SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_GROUP 16
#define SCRAPE_LLVM_NAME_CAPACITY 96
#define SCRAPE_LLVM_SCOPE_DEPTH 64

STRUCT(ScrapeLlvmFileList)
{
    StringOs paths[SCRAPE_LLVM_MAX_DISCOVERED_FILES];
    u32 count;
    u8 reserved[4];
};

STRUCT(ScrapeLlvmInstruction)
{
    char name[SCRAPE_LLVM_NAME_CAPACITY];
    bool is_multiclass;
    u8 reserved[3];
};

STRUCT(ScrapeLlvmProcessorModel)
{
    char name[SCRAPE_LLVM_NAME_CAPACITY];
    u32 issue_width;
    u32 micro_op_buffer_size;
    u32 loop_micro_op_buffer_size;
    u32 load_latency;
    u32 vector_load_latency;
    u32 store_latency;
    u32 high_latency;
    u32 mispredict_penalty;
    bool complete_model;
    u8 reserved[3];
};

STRUCT(ScrapeLlvmProcessorResource)
{
    char name[SCRAPE_LLVM_NAME_CAPACITY];
    u16 model_index;
    u16 member_count;
    u32 units;
    u32 buffer_size;
    bool is_group;
    u8 reserved[3];
    char member_names[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_GROUP][SCRAPE_LLVM_NAME_CAPACITY];
};

STRUCT(ScrapeLlvmReadAdvance)
{
    char name[SCRAPE_LLVM_NAME_CAPACITY];
    u16 model_index;
    s16 cycles;
};

STRUCT(ScrapeLlvmScheduleWrite)
{
    char name[SCRAPE_LLVM_NAME_CAPACITY];
    char primary_write_name[SCRAPE_LLVM_NAME_CAPACITY];
    char resource_names[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE][SCRAPE_LLVM_NAME_CAPACITY];
    u32 release_cycles[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE];
    u32 latency;
    u32 micro_op_count;
    u16 model_index;
    u16 resource_name_count;
    bool has_metrics;
    bool is_variant;
    u8 reserved[2];
};

STRUCT(ScrapeLlvmInstructionSchedule)
{
    char instruction_name[SCRAPE_LLVM_NAME_CAPACITY];
    char schedule_write_name[SCRAPE_LLVM_NAME_CAPACITY];
    char read_advance_names[SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT_PER_SCHEDULE][SCRAPE_LLVM_NAME_CAPACITY];
    u16 model_index;
    u16 read_advance_count;
};

STRUCT(ScrapeLlvmResolvedCost)
{
    u32 latency;
    u32 micro_op_count;
    bool has_metrics;
    u8 reserved[3];
};

STRUCT(ScrapeLlvmLoweringCostSpec)
{
    String8 lowering_name;
    String8 instruction_names[3];
    String8 schedule_write_names[2];
    u32 instruction_name_count;
    u32 schedule_write_name_count;
};

static ScrapeLlvmLoweringCostSpec scrape_llvm_lowering_cost_specs[] = {
    {
        .lowering_name = S8("X86_SELECTOR_LOWERING_XOR_ZERO_GPR32"),
        .instruction_names = { S8("XOR32rr") },
        .instruction_name_count = 1,
    },
    {
        .lowering_name = S8("X86_SELECTOR_LOWERING_MOV_IMMEDIATE_GPR"),
        .instruction_names = { S8("MOV64ri"), S8("MOV64ri32") },
        .instruction_name_count = 2,
        .schedule_write_names = { S8("WriteMove") },
        .schedule_write_name_count = 1,
    },
    {
        .lowering_name = S8("X86_SELECTOR_LOWERING_MOV_REGISTER_GPR"),
        .instruction_names = { S8("MOV64rr") },
        .instruction_name_count = 1,
        .schedule_write_names = { S8("WriteMove") },
        .schedule_write_name_count = 1,
    },
    {
        .lowering_name = S8("X86_SELECTOR_LOWERING_ADD_REGISTER_GPR"),
        .instruction_names = { S8("ADD64rr") },
        .instruction_name_count = 1,
        .schedule_write_names = { S8("WriteALU") },
        .schedule_write_name_count = 1,
    },
    {
        .lowering_name = S8("X86_SELECTOR_LOWERING_ADD_IMMEDIATE_GPR"),
        .instruction_names = { S8("ADD64rr") },
        .instruction_name_count = 1,
        .schedule_write_names = { S8("WriteALU") },
        .schedule_write_name_count = 1,
    },
};

STRUCT(ScrapeLlvmDatabase)
{
    ScrapeLlvmInstruction instructions[SCRAPE_LLVM_MAX_INSTRUCTION_COUNT];
    ScrapeLlvmProcessorModel processor_models[SCRAPE_LLVM_MAX_PROCESSOR_MODEL_COUNT];
    ScrapeLlvmProcessorResource processor_resources[SCRAPE_LLVM_MAX_PROCESSOR_RESOURCE_COUNT];
    ScrapeLlvmReadAdvance read_advances[SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT];
    ScrapeLlvmScheduleWrite schedule_writes[SCRAPE_LLVM_MAX_SCHEDULE_WRITE_COUNT];
    ScrapeLlvmInstructionSchedule instruction_schedules[SCRAPE_LLVM_MAX_INSTRUCTION_SCHEDULE_COUNT];
    u32 instruction_count;
    u32 processor_model_count;
    u32 processor_resource_count;
    u32 read_advance_count;
    u32 schedule_write_count;
    u32 instruction_schedule_count;
};

STRUCT(ScrapeLlvmProgramState)
{
    ProgramState general_program_state;
    StringOs llvm_root;
    StringOs llvm_tblgen_path;
    StringOs generate_output;
    bool generate;
    bool skip_generation;
    u8 reserved[6];
};

ENUM(ScrapeLlvmJsonKind,
    SCRAPE_LLVM_JSON_KIND_NULL,
    SCRAPE_LLVM_JSON_KIND_BOOL,
    SCRAPE_LLVM_JSON_KIND_INTEGER,
    SCRAPE_LLVM_JSON_KIND_STRING,
    SCRAPE_LLVM_JSON_KIND_ARRAY,
    SCRAPE_LLVM_JSON_KIND_OBJECT,
);

STRUCT(ScrapeLlvmJsonValue);

STRUCT(ScrapeLlvmJsonObjectEntry)
{
    String8 key;
    ScrapeLlvmJsonValue* value;
    ScrapeLlvmJsonObjectEntry* next;
};

STRUCT(ScrapeLlvmJsonArrayItem)
{
    ScrapeLlvmJsonValue* value;
    ScrapeLlvmJsonArrayItem* next;
};

STRUCT(ScrapeLlvmJsonObject)
{
    ScrapeLlvmJsonObjectEntry* first;
    u32 count;
    u8 reserved[4];
};

STRUCT(ScrapeLlvmJsonArray)
{
    ScrapeLlvmJsonArrayItem* first;
    u32 count;
    u8 reserved[4];
};

STRUCT(ScrapeLlvmJsonValue)
{
    ScrapeLlvmJsonKind kind;
    u8 reserved[4];
    union
    {
        bool bool_value;
        s64 integer_value;
        String8 string_value;
        ScrapeLlvmJsonArray array_value;
        ScrapeLlvmJsonObject object_value;
    };
};

STRUCT(ScrapeLlvmJsonParser)
{
    Arena* arena;
    String8 text;
    u64 cursor;
};

STRUCT(ScrapeLlvmRecordMapEntry)
{
    String8 key;
    ScrapeLlvmJsonValue* value;
    bool is_occupied;
    u8 reserved[7];
};

STRUCT(ScrapeLlvmRecordMap)
{
    ScrapeLlvmRecordMapEntry* entries;
    u32 capacity;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL ScrapeLlvmProgramState scrape_llvm_program_state = {};
BUSTER_IMPL ProgramState* program_state = &scrape_llvm_program_state.general_program_state;

BUSTER_GLOBAL_LOCAL bool scrape_llvm_ascii_is_space(u8 character);
BUSTER_GLOBAL_LOCAL bool scrape_llvm_ascii_is_digit(u8 character);
BUSTER_GLOBAL_LOCAL String8 scrape_llvm_name_string8(char const* text);
BUSTER_GLOBAL_LOCAL void scrape_llvm_add_instruction_schedule(ScrapeLlvmDatabase* database,
                                                              u32 model_index,
                                                              String8 instruction_name,
                                                              String8 schedule_write_name,
                                                              String8* read_advance_names,
                                                              u32 read_advance_count);

BUSTER_GLOBAL_LOCAL StringOs scrape_llvm_default_tblgen_path()
{
    StringOs result = SOs("/home/david/dev/toolchain/install/llvm_22.1.0_x86_64-linux-Release/bin/llvm-tblgen");
    return result;
}

BUSTER_GLOBAL_LOCAL u64 scrape_llvm_hash_string(String8 text)
{
    u64 result = 1469598103934665603ull;
    for (u64 index = 0; index < text.length; index += 1)
    {
        result ^= text.pointer[index];
        result *= 1099511628211ull;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_json_skip_whitespace(ScrapeLlvmJsonParser* parser)
{
    while (parser->cursor < parser->text.length && scrape_llvm_ascii_is_space(parser->text.pointer[parser->cursor]))
    {
        parser->cursor += 1;
    }
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_parse_value(ScrapeLlvmJsonParser* parser);

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_json_parse_string(ScrapeLlvmJsonParser* parser)
{
    String8 result = { 0 };

    if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == '"')
    {
        parser->cursor += 1;
        u64 start = parser->cursor;
        bool requires_copy = false;

        while (parser->cursor < parser->text.length)
        {
            u8 character = parser->text.pointer[parser->cursor];
            if (character == '\\')
            {
                requires_copy = true;
                parser->cursor += 2;
            }
            else if (character == '"')
            {
                break;
            }
            else
            {
                parser->cursor += 1;
            }
        }

        u64 end = parser->cursor;
        if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == '"')
        {
            if (!requires_copy)
            {
                result = string8_from_pointer_length(parser->text.pointer + start, end - start);
            }
            else
            {
                char8* buffer = arena_allocate(parser->arena, char8, end - start + 1);
                u64 write_index = 0;
                for (u64 read_index = start; read_index < end; read_index += 1)
                {
                    u8 character = parser->text.pointer[read_index];
                    if (character == '\\' && read_index + 1 < end)
                    {
                        read_index += 1;
                        u8 escaped = parser->text.pointer[read_index];
                        switch (escaped)
                        {
                            case '"': buffer[write_index] = '"'; break;
                            case '\\': buffer[write_index] = '\\'; break;
                            case '/': buffer[write_index] = '/'; break;
                            case 'b': buffer[write_index] = '\b'; break;
                            case 'f': buffer[write_index] = '\f'; break;
                            case 'n': buffer[write_index] = '\n'; break;
                            case 'r': buffer[write_index] = '\r'; break;
                            case 't': buffer[write_index] = '\t'; break;
                            default: buffer[write_index] = (char8)escaped; break;
                        }
                    }
                    else
                    {
                        buffer[write_index] = (char8)character;
                    }
                    write_index += 1;
                }
                buffer[write_index] = 0;
                result = string8_from_pointer_length(buffer, write_index);
            }
            parser->cursor += 1;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_allocate_value(ScrapeLlvmJsonParser* parser, ScrapeLlvmJsonKind kind)
{
    ScrapeLlvmJsonValue* result = arena_allocate(parser->arena, ScrapeLlvmJsonValue, 1);
    if (result)
    {
        memset(result, 0, sizeof(*result));
        result->kind = kind;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_parse_object(ScrapeLlvmJsonParser* parser)
{
    ScrapeLlvmJsonValue* result = scrape_llvm_json_allocate_value(parser, SCRAPE_LLVM_JSON_KIND_OBJECT);
    if (result && parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == '{')
    {
        parser->cursor += 1;
        scrape_llvm_json_skip_whitespace(parser);
        ScrapeLlvmJsonObjectEntry** next_entry = &result->object_value.first;

        while (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] != '}')
        {
            String8 key = scrape_llvm_json_parse_string(parser);
            scrape_llvm_json_skip_whitespace(parser);
            if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == ':')
            {
                parser->cursor += 1;
            }
            scrape_llvm_json_skip_whitespace(parser);

            ScrapeLlvmJsonObjectEntry* entry = arena_allocate(parser->arena, ScrapeLlvmJsonObjectEntry, 1);
            entry->key = key;
            entry->value = scrape_llvm_json_parse_value(parser);
            entry->next = 0;
            *next_entry = entry;
            next_entry = &entry->next;
            result->object_value.count += 1;

            scrape_llvm_json_skip_whitespace(parser);
            if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == ',')
            {
                parser->cursor += 1;
                scrape_llvm_json_skip_whitespace(parser);
            }
        }

        if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == '}')
        {
            parser->cursor += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_parse_array(ScrapeLlvmJsonParser* parser)
{
    ScrapeLlvmJsonValue* result = scrape_llvm_json_allocate_value(parser, SCRAPE_LLVM_JSON_KIND_ARRAY);
    if (result && parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == '[')
    {
        parser->cursor += 1;
        scrape_llvm_json_skip_whitespace(parser);
        ScrapeLlvmJsonArrayItem** next_item = &result->array_value.first;

        while (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] != ']')
        {
            ScrapeLlvmJsonArrayItem* item = arena_allocate(parser->arena, ScrapeLlvmJsonArrayItem, 1);
            item->value = scrape_llvm_json_parse_value(parser);
            item->next = 0;
            *next_item = item;
            next_item = &item->next;
            result->array_value.count += 1;

            scrape_llvm_json_skip_whitespace(parser);
            if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == ',')
            {
                parser->cursor += 1;
                scrape_llvm_json_skip_whitespace(parser);
            }
        }

        if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == ']')
        {
            parser->cursor += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_parse_number(ScrapeLlvmJsonParser* parser)
{
    ScrapeLlvmJsonValue* result = scrape_llvm_json_allocate_value(parser, SCRAPE_LLVM_JSON_KIND_INTEGER);
    s64 sign = 1;
    if (parser->cursor < parser->text.length && parser->text.pointer[parser->cursor] == '-')
    {
        sign = -1;
        parser->cursor += 1;
    }

    s64 value = 0;
    while (parser->cursor < parser->text.length && scrape_llvm_ascii_is_digit(parser->text.pointer[parser->cursor]))
    {
        value = value * 10 + (s64)(parser->text.pointer[parser->cursor] - '0');
        parser->cursor += 1;
    }
    result->integer_value = sign * value;
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_parse_value(ScrapeLlvmJsonParser* parser)
{
    scrape_llvm_json_skip_whitespace(parser);

    ScrapeLlvmJsonValue* result = 0;
    if (parser->cursor < parser->text.length)
    {
        u8 character = parser->text.pointer[parser->cursor];
        if (character == '{')
        {
            result = scrape_llvm_json_parse_object(parser);
        }
        else if (character == '[')
        {
            result = scrape_llvm_json_parse_array(parser);
        }
        else if (character == '"')
        {
            result = scrape_llvm_json_allocate_value(parser, SCRAPE_LLVM_JSON_KIND_STRING);
            result->string_value = scrape_llvm_json_parse_string(parser);
        }
        else if (character == '-' || scrape_llvm_ascii_is_digit(character))
        {
            result = scrape_llvm_json_parse_number(parser);
        }
        else if (string8_starts_with_sequence(BUSTER_SLICE_START(parser->text, parser->cursor), S8("true")))
        {
            result = scrape_llvm_json_allocate_value(parser, SCRAPE_LLVM_JSON_KIND_BOOL);
            result->bool_value = true;
            parser->cursor += 4;
        }
        else if (string8_starts_with_sequence(BUSTER_SLICE_START(parser->text, parser->cursor), S8("false")))
        {
            result = scrape_llvm_json_allocate_value(parser, SCRAPE_LLVM_JSON_KIND_BOOL);
            result->bool_value = false;
            parser->cursor += 5;
        }
        else if (string8_starts_with_sequence(BUSTER_SLICE_START(parser->text, parser->cursor), S8("null")))
        {
            result = scrape_llvm_json_allocate_value(parser, SCRAPE_LLVM_JSON_KIND_NULL);
            parser->cursor += 4;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_object_find(ScrapeLlvmJsonValue* object, String8 key)
{
    ScrapeLlvmJsonValue* result = 0;
    if (object && object->kind == SCRAPE_LLVM_JSON_KIND_OBJECT)
    {
        for (ScrapeLlvmJsonObjectEntry* entry = object->object_value.first; entry; entry = entry->next)
        {
            if (string_equal(entry->key, key))
            {
                result = entry->value;
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_json_array_at(ScrapeLlvmJsonValue* array, u32 index)
{
    ScrapeLlvmJsonValue* result = 0;
    if (array && array->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        u32 i = 0;
        for (ScrapeLlvmJsonArrayItem* item = array->array_value.first; item; item = item->next, i += 1)
        {
            if (i == index)
            {
                result = item->value;
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_json_string(ScrapeLlvmJsonValue* value)
{
    String8 result = { 0 };
    if (value && value->kind == SCRAPE_LLVM_JSON_KIND_STRING)
    {
        result = value->string_value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s64 scrape_llvm_json_integer(ScrapeLlvmJsonValue* value, s64 fallback)
{
    s64 result = fallback;
    if (value && value->kind == SCRAPE_LLVM_JSON_KIND_INTEGER)
    {
        result = value->integer_value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_json_def_name(ScrapeLlvmJsonValue* value)
{
    String8 result = { 0 };
    if (value && value->kind == SCRAPE_LLVM_JSON_KIND_OBJECT)
    {
        result = scrape_llvm_json_string(scrape_llvm_json_object_find(value, S8("def")));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmRecordMap scrape_llvm_build_record_map(Arena* arena, ScrapeLlvmJsonValue* root)
{
    ScrapeLlvmRecordMap result = { 0 };
    if (root && root->kind == SCRAPE_LLVM_JSON_KIND_OBJECT)
    {
        u32 capacity = 1;
        while (capacity < root->object_value.count * 2)
        {
            capacity <<= 1;
        }

        result.entries = arena_allocate(arena, ScrapeLlvmRecordMapEntry, capacity);
        result.capacity = capacity;
        memset(result.entries, 0, sizeof(*result.entries) * capacity);

        for (ScrapeLlvmJsonObjectEntry* entry = root->object_value.first; entry; entry = entry->next)
        {
            u32 slot = (u32)(scrape_llvm_hash_string(entry->key) & (capacity - 1));
            while (result.entries[slot].is_occupied)
            {
                slot = (slot + 1) & (capacity - 1);
            }
            result.entries[slot].key = entry->key;
            result.entries[slot].value = entry->value;
            result.entries[slot].is_occupied = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmJsonValue* scrape_llvm_record_map_find(ScrapeLlvmRecordMap map, String8 key)
{
    ScrapeLlvmJsonValue* result = 0;
    if (map.entries && map.capacity != 0)
    {
        u32 slot = (u32)(scrape_llvm_hash_string(key) & (map.capacity - 1));
        for (u32 probe = 0; probe < map.capacity; probe += 1)
        {
            ScrapeLlvmRecordMapEntry* entry = &map.entries[slot];
            if (!entry->is_occupied)
            {
                break;
            }
            if (string_equal(entry->key, key))
            {
                result = entry->value;
                break;
            }
            slot = (slot + 1) & (map.capacity - 1);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_json_array_collect_def_names(ScrapeLlvmJsonValue* array, String8* out_names, u32 out_name_capacity, u32* out_name_count)
{
    bool result = array && array->kind == SCRAPE_LLVM_JSON_KIND_ARRAY;
    u32 name_count = 0;
    if (result)
    {
        for (ScrapeLlvmJsonArrayItem* item = array->array_value.first; item && name_count < out_name_capacity; item = item->next)
        {
            String8 name = scrape_llvm_json_def_name(item->value);
            if (name.length > 0)
            {
                out_names[name_count] = name;
                name_count += 1;
            }
        }
    }
    if (out_name_count)
    {
        *out_name_count = name_count;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice scrape_llvm_run_tblgen_dump_json(Arena* arena, StringOs llvm_root, StringOs llvm_tblgen_path)
{
    ByteSlice result = { 0 };

    StringOs x86_td_parts[] = {
        llvm_root,
        SOs("/llvm/lib/Target/X86/X86.td"),
    };
    StringOs include_x86_parts[] = {
        llvm_root,
        SOs("/llvm/lib/Target/X86"),
    };
    StringOs include_llvm_parts[] = {
        llvm_root,
        SOs("/llvm/include"),
    };
    StringOs include_target_parts[] = {
        llvm_root,
        SOs("/llvm/lib/Target"),
    };

    StringOs x86_td_path = string_os_join_arena(arena, (StringOsSlice)BUSTER_ARRAY_TO_SLICE(x86_td_parts), true);
    StringOs include_x86_path = string_os_join_arena(arena, (StringOsSlice)BUSTER_ARRAY_TO_SLICE(include_x86_parts), true);
    StringOs include_llvm_path = string_os_join_arena(arena, (StringOsSlice)BUSTER_ARRAY_TO_SLICE(include_llvm_parts), true);
    StringOs include_target_path = string_os_join_arena(arena, (StringOsSlice)BUSTER_ARRAY_TO_SLICE(include_target_parts), true);

    StringOs arguments[] = {
        llvm_tblgen_path,
        SOs("--dump-json"),
        SOs("-I"),
        include_x86_path,
        SOs("-I"),
        include_llvm_path,
        SOs("-I"),
        include_target_path,
        x86_td_path,
    };

    StringOsList argv = string_os_list_create_from(arena, (StringOsSlice)BUSTER_ARRAY_TO_SLICE(arguments));
    ProcessSpawnResult spawn = os_process_spawn(llvm_tblgen_path, argv, program_state->input.envp, (ProcessSpawnOptions){ .capture = ((1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR)) });
    ProcessWaitResult wait = os_process_wait_sync(arena, spawn);
    if (wait.result == PROCESS_RESULT_SUCCESS)
    {
        result = wait.streams[STANDARD_STREAM_OUTPUT];
    }
    else if (wait.streams[STANDARD_STREAM_ERROR].length != 0)
    {
        string8_print(S8("llvm-tblgen failed:\n{S8}\n"), BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]));
    }

    return result;
}

#if !defined(_WIN32)
BUSTER_GLOBAL_LOCAL void scrape_llvm_add_instruction_schedules_for_regex(ScrapeLlvmDatabase* database,
                                                                         u32 model_index,
                                                                         String8 pattern,
                                                                         String8 schedule_write_name,
                                                                         String8* read_advance_names,
                                                                         u32 read_advance_count)
{
    char8* regex_text = arena_allocate(thread_arena(), char8, pattern.length + 3);
    regex_text[0] = '^';
    memcpy(regex_text + 1, pattern.pointer, pattern.length);
    regex_text[pattern.length + 1] = '$';
    regex_text[pattern.length + 2] = 0;

    regex_t compiled = { 0 };
    if (regcomp(&compiled, (char const*)regex_text, REG_EXTENDED | REG_NOSUB) == 0)
    {
        for (u32 instruction_i = 0; instruction_i < database->instruction_count; instruction_i += 1)
        {
            String8 instruction_name = scrape_llvm_name_string8(database->instructions[instruction_i].name);
            if (instruction_name.length > 0 && regexec(&compiled, (char const*)instruction_name.pointer, 0, 0, 0) == 0)
            {
                scrape_llvm_add_instruction_schedule(database, model_index, instruction_name, schedule_write_name, read_advance_names, read_advance_count);
            }
        }
        regfree(&compiled);
    }
}
#endif

BUSTER_GLOBAL_LOCAL bool scrape_llvm_ascii_is_space(u8 character)
{
    bool result = character == ' ' ||
                  character == '\t' ||
                  character == '\n' ||
                  character == '\r' ||
                  character == '\v' ||
                  character == '\f';
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_ascii_is_identifier(u8 character)
{
    bool result = (character >= 'a' && character <= 'z') ||
                  (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') ||
                  character == '_' ||
                  character == '.';
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_ascii_is_digit(u8 character)
{
    bool result = character >= '0' && character <= '9';
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_ascii_is_identifier_or_sign(u8 character)
{
    bool result = scrape_llvm_ascii_is_identifier(character) || character == '-';
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_trim(String8 text)
{
    while (text.length > 0 && scrape_llvm_ascii_is_space(text.pointer[0]))
    {
        text.pointer += 1;
        text.length -= 1;
    }

    while (text.length > 0 && scrape_llvm_ascii_is_space(text.pointer[text.length - 1]))
    {
        text.length -= 1;
    }

    return text;
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_copy_name(char destination[SCRAPE_LLVM_NAME_CAPACITY], String8 source)
{
    u64 copy_count = source.length;
    if (copy_count >= SCRAPE_LLVM_NAME_CAPACITY)
    {
        copy_count = SCRAPE_LLVM_NAME_CAPACITY - 1;
    }

    memset(destination, 0, SCRAPE_LLVM_NAME_CAPACITY);
    if (copy_count > 0)
    {
        memcpy(destination, source.pointer, copy_count);
    }
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_preferred_model_name()
{
    return S8("Znver4Model");
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_name_string8(char const* text)
{
    return string8_from_pointer((char8*)text);
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_name_equals(char const* text, String8 candidate)
{
    return string_equal(scrape_llvm_name_string8(text), candidate);
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_find_processor_model_index(ScrapeLlvmDatabase* database, String8 name)
{
    u32 result = 0xffffffffu;
    for (u32 model_i = 0; model_i < database->processor_model_count; model_i += 1)
    {
        if (scrape_llvm_name_equals(database->processor_models[model_i].name, name))
        {
            result = model_i;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_add_processor_model(ScrapeLlvmDatabase* database, String8 name)
{
    u32 result = scrape_llvm_find_processor_model_index(database, name);
    if (result == 0xffffffffu && database->processor_model_count < SCRAPE_LLVM_MAX_PROCESSOR_MODEL_COUNT)
    {
        result = database->processor_model_count;
        database->processor_model_count += 1;
        scrape_llvm_copy_name(database->processor_models[result].name, name);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_find_processor_resource_index(ScrapeLlvmDatabase* database, u32 model_index, String8 name)
{
    u32 result = 0xffffffffu;
    for (u32 resource_i = 0; resource_i < database->processor_resource_count; resource_i += 1)
    {
        ScrapeLlvmProcessorResource* resource = &database->processor_resources[resource_i];
        if (resource->model_index == model_index && scrape_llvm_name_equals(resource->name, name))
        {
            result = resource_i;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_find_read_advance_index(ScrapeLlvmDatabase* database, u32 model_index, String8 name)
{
    u32 result = 0xffffffffu;
    for (u32 read_advance_i = 0; read_advance_i < database->read_advance_count; read_advance_i += 1)
    {
        ScrapeLlvmReadAdvance* read_advance = &database->read_advances[read_advance_i];
        if (read_advance->model_index == model_index && scrape_llvm_name_equals(read_advance->name, name))
        {
            result = read_advance_i;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_first_identifier(String8 text)
{
    String8 result = { 0 };
    u64 index = 0;
    while (index < text.length && !scrape_llvm_ascii_is_identifier(text.pointer[index]))
    {
        index += 1;
    }

    u64 start = index;
    while (index < text.length && scrape_llvm_ascii_is_identifier(text.pointer[index]))
    {
        index += 1;
    }

    if (index > start)
    {
        result = string8_from_pointer_length(text.pointer + start, index - start);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_next_identifier_or_sign(String8 text, u64* in_out_cursor)
{
    String8 result = { 0 };
    u64 index = *in_out_cursor;
    while (index < text.length && !scrape_llvm_ascii_is_identifier_or_sign(text.pointer[index]))
    {
        index += 1;
    }

    u64 start = index;
    while (index < text.length && scrape_llvm_ascii_is_identifier_or_sign(text.pointer[index]))
    {
        index += 1;
    }

    if (index > start)
    {
        result = string8_from_pointer_length(text.pointer + start, index - start);
    }

    *in_out_cursor = index;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_identifier_after_keyword(String8 line, String8 keyword)
{
    String8 result = { 0 };
    u64 match = string8_first_sequence(line, keyword);
    if (match != BUSTER_STRING_NO_MATCH)
    {
        String8 remainder = string8_from_pointer_length(line.pointer + match + keyword.length, line.length - match - keyword.length);
        remainder = scrape_llvm_trim(remainder);
        result = scrape_llvm_first_identifier(remainder);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 scrape_llvm_find_matching_character(String8 text, u64 start, u8 open_character, u8 close_character)
{
    u64 result = BUSTER_STRING_NO_MATCH;
    if (start < text.length && text.pointer[start] == open_character)
    {
        u32 depth = 1;
        bool in_string = false;
        for (u64 index = start + 1; index < text.length; index += 1)
        {
            u8 character = text.pointer[index];
            if (character == '"' && (index == 0 || text.pointer[index - 1] != '\\'))
            {
                in_string = !in_string;
            }
            else if (!in_string)
            {
                if (character == open_character)
                {
                    depth += 1;
                }
                else if (character == close_character)
                {
                    depth -= 1;
                    if (depth == 0)
                    {
                        result = index;
                        break;
                    }
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_extract_u32_after(String8 text, String8 marker, u32* out_value)
{
    bool result = false;
    u64 found = string8_first_sequence(text, marker);
    if (found != BUSTER_STRING_NO_MATCH)
    {
        u64 cursor = found + marker.length;
        while (cursor < text.length && !scrape_llvm_ascii_is_digit(text.pointer[cursor]))
        {
            cursor += 1;
        }

        u32 value = 0;
        u64 digit_count = 0;
        while (cursor < text.length && scrape_llvm_ascii_is_digit(text.pointer[cursor]))
        {
            value = value * 10 + (u32)(text.pointer[cursor] - '0');
            cursor += 1;
            digit_count += 1;
        }

        if (digit_count > 0)
        {
            *out_value = value;
            result = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_parse_s32(String8 text, s32* out_value)
{
    bool result = false;
    if (text.length > 0)
    {
        s32 sign = 1;
        u64 index = 0;
        if (text.pointer[index] == '-')
        {
            sign = -1;
            index += 1;
        }

        if (index < text.length && scrape_llvm_ascii_is_digit(text.pointer[index]))
        {
            s32 value = 0;
            while (index < text.length && scrape_llvm_ascii_is_digit(text.pointer[index]))
            {
                value = value * 10 + (s32)(text.pointer[index] - '0');
                index += 1;
            }
            *out_value = sign * value;
            result = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_model_property_value(ScrapeLlvmProcessorModel* model, String8 property_name, u32* out_value)
{
    bool result = true;
    if (string_equal(property_name, S8("IssueWidth")))
    {
        *out_value = model->issue_width;
    }
    else if (string_equal(property_name, S8("MicroOpBufferSize")))
    {
        *out_value = model->micro_op_buffer_size;
    }
    else if (string_equal(property_name, S8("LoopMicroOpBufferSize")))
    {
        *out_value = model->loop_micro_op_buffer_size;
    }
    else if (string_equal(property_name, S8("LoadLatency")))
    {
        *out_value = model->load_latency;
    }
    else if (string_equal(property_name, S8("VecLoadLatency")))
    {
        *out_value = model->vector_load_latency;
    }
    else if (string_equal(property_name, S8("StoreLatency")))
    {
        *out_value = model->store_latency;
    }
    else if (string_equal(property_name, S8("HighLatency")))
    {
        *out_value = model->high_latency;
    }
    else if (string_equal(property_name, S8("MispredictPenalty")))
    {
        *out_value = model->mispredict_penalty;
    }
    else
    {
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_resolve_model_u32(ScrapeLlvmDatabase* database, u32 fallback_model_index, String8 value_text, u32* out_value)
{
    bool result = false;
    s32 signed_value = 0;
    if (scrape_llvm_parse_s32(value_text, &signed_value) && signed_value >= 0)
    {
        *out_value = (u32)signed_value;
        result = true;
    }
    if (!result)
    {
        u64 dot_index = string8_first_sequence(value_text, S8("."));
        if (dot_index != BUSTER_STRING_NO_MATCH)
        {
            String8 model_name = string8_from_pointer_length(value_text.pointer, dot_index);
            String8 property_name = string8_from_pointer_length(value_text.pointer + dot_index + 1, value_text.length - dot_index - 1);
            u32 model_index = scrape_llvm_find_processor_model_index(database, model_name);
            if (model_index == 0xffffffffu)
            {
                model_index = fallback_model_index;
            }
            if (model_index != 0xffffffffu)
            {
                result = scrape_llvm_model_property_value(&database->processor_models[model_index], property_name, out_value);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_find_files_recursive(Arena* arena, StringOs directory, String8 suffix, ScrapeLlvmFileList* list)
{
    DIR* dir = opendir((char*)directory.pointer);
    if (!dir)
    {
        return;
    }

    struct dirent* entry = 0;
    while ((entry = readdir(dir)) != 0)
    {
        if (entry->d_name[0] == '.')
        {
            continue;
        }

        String8 entry_name = string8_from_pointer((char8*)entry->d_name);
        StringOs parts[] = { directory, SOs("/"), entry_name };
        StringOs full_path = string_os_join_arena(arena, (StringOsSlice)BUSTER_ARRAY_TO_SLICE(parts), true);

        struct stat stat_buffer = { 0 };
        if (stat((char*)full_path.pointer, &stat_buffer) != 0)
        {
            continue;
        }

        if (S_ISDIR(stat_buffer.st_mode))
        {
            scrape_llvm_find_files_recursive(arena, full_path, suffix, list);
        }
        else if (S_ISREG(stat_buffer.st_mode) && string8_ends_with_sequence(entry_name, suffix) && list->count < SCRAPE_LLVM_MAX_DISCOVERED_FILES)
        {
            list->paths[list->count] = full_path;
            list->count += 1;
        }
    }

    closedir(dir);
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_count_braces_outside_quotes(String8 text, u32* out_open_count, u32* out_close_count)
{
    u32 open_count = 0;
    u32 close_count = 0;
    bool in_string = false;
    for (u64 index = 0; index < text.length; index += 1)
    {
        u8 character = text.pointer[index];
        if (character == '"' && (index == 0 || text.pointer[index - 1] != '\\'))
        {
            in_string = !in_string;
        }
        else if (!in_string)
        {
            if (character == '{')
            {
                open_count += 1;
            }
            else if (character == '}')
            {
                close_count += 1;
            }
        }
    }

    *out_open_count = open_count;
    *out_close_count = close_count;
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_add_instruction(ScrapeLlvmDatabase* database, String8 name, bool is_multiclass)
{
    if (name.length == 0 || name.pointer[0] == ':')
    {
        return;
    }

    if (database->instruction_count < SCRAPE_LLVM_MAX_INSTRUCTION_COUNT)
    {
        ScrapeLlvmInstruction* instruction = &database->instructions[database->instruction_count];
        database->instruction_count += 1;
        scrape_llvm_copy_name(instruction->name, name);
        instruction->is_multiclass = true;
        if (!is_multiclass)
        {
            instruction->is_multiclass = false;
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_add_processor_resource(ScrapeLlvmDatabase* database,
                                                            u32 model_index,
                                                            String8 name,
                                                            bool is_group,
                                                            u32 units,
                                                            u32 buffer_size,
                                                            String8* member_names,
                                                            u32 member_count)
{
    if (name.length == 0 || model_index == 0xffffffffu)
    {
        return;
    }

    u32 resource_index = scrape_llvm_find_processor_resource_index(database, model_index, name);
    if (resource_index == 0xffffffffu && database->processor_resource_count < SCRAPE_LLVM_MAX_PROCESSOR_RESOURCE_COUNT)
    {
        resource_index = database->processor_resource_count;
        database->processor_resource_count += 1;
        scrape_llvm_copy_name(database->processor_resources[resource_index].name, name);
        database->processor_resources[resource_index].model_index = (u16)model_index;
    }

    if (resource_index != 0xffffffffu)
    {
        ScrapeLlvmProcessorResource* resource = &database->processor_resources[resource_index];
        resource->is_group = is_group;
        resource->units = units;
        resource->buffer_size = buffer_size;
        resource->member_count = (u16)BUSTER_MIN(member_count, SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_GROUP);
        for (u32 member_i = 0; member_i < resource->member_count; member_i += 1)
        {
            scrape_llvm_copy_name(resource->member_names[member_i], member_names[member_i]);
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_add_read_advance(ScrapeLlvmDatabase* database, u32 model_index, String8 name, s32 cycles)
{
    if (name.length == 0 || model_index == 0xffffffffu)
    {
        return;
    }

    u32 read_advance_index = scrape_llvm_find_read_advance_index(database, model_index, name);
    if (read_advance_index == 0xffffffffu && database->read_advance_count < SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT)
    {
        read_advance_index = database->read_advance_count;
        database->read_advance_count += 1;
        scrape_llvm_copy_name(database->read_advances[read_advance_index].name, name);
        database->read_advances[read_advance_index].model_index = (u16)model_index;
    }

    if (read_advance_index != 0xffffffffu)
    {
        database->read_advances[read_advance_index].cycles = (s16)cycles;
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_add_schedule_write(ScrapeLlvmDatabase* database,
                                                        u32 model_index,
                                                        String8 name,
                                                        u32 latency,
                                                        u32 micro_op_count,
                                                        bool has_metrics,
                                                        bool is_variant,
                                                        String8 primary_write_name,
                                                        String8* resource_names,
                                                        u32* release_cycles,
                                                        u32 resource_name_count)
{
    if (name.length == 0 || model_index == 0xffffffffu)
    {
        return;
    }

    u32 write_index = 0xffffffffu;
    for (u32 candidate_i = 0; candidate_i < database->schedule_write_count; candidate_i += 1)
    {
        ScrapeLlvmScheduleWrite* candidate = &database->schedule_writes[candidate_i];
        if (candidate->model_index == model_index && scrape_llvm_name_equals(candidate->name, name))
        {
            write_index = candidate_i;
            break;
        }
    }

    if (write_index == 0xffffffffu && database->schedule_write_count < SCRAPE_LLVM_MAX_SCHEDULE_WRITE_COUNT)
    {
        write_index = database->schedule_write_count;
        database->schedule_write_count += 1;
        scrape_llvm_copy_name(database->schedule_writes[write_index].name, name);
        database->schedule_writes[write_index].model_index = (u16)model_index;
    }

    if (write_index != 0xffffffffu)
    {
        ScrapeLlvmScheduleWrite* write = &database->schedule_writes[write_index];
        scrape_llvm_copy_name(write->name, name);
        if (has_metrics)
        {
            write->latency = latency;
            write->micro_op_count = micro_op_count;
            write->has_metrics = true;
        }
        if (is_variant)
        {
            write->is_variant = true;
            if (primary_write_name.length > 0)
            {
                scrape_llvm_copy_name(write->primary_write_name, primary_write_name);
            }
        }
        else if (primary_write_name.length > 0)
        {
            scrape_llvm_copy_name(write->primary_write_name, primary_write_name);
        }

        write->resource_name_count = (u16)BUSTER_MIN(resource_name_count, SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE);
        for (u32 resource_i = 0; resource_i < write->resource_name_count; resource_i += 1)
        {
            scrape_llvm_copy_name(write->resource_names[resource_i], resource_names[resource_i]);
            write->release_cycles[resource_i] = release_cycles ? release_cycles[resource_i] : 0;
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_add_instruction_schedule(ScrapeLlvmDatabase* database,
                                                              u32 model_index,
                                                              String8 instruction_name,
                                                              String8 schedule_write_name,
                                                              String8* read_advance_names,
                                                              u32 read_advance_count)
{
    if (instruction_name.length == 0 || schedule_write_name.length == 0 || model_index == 0xffffffffu)
    {
        return;
    }

    if (database->instruction_schedule_count < SCRAPE_LLVM_MAX_INSTRUCTION_SCHEDULE_COUNT)
    {
        ScrapeLlvmInstructionSchedule* schedule = &database->instruction_schedules[database->instruction_schedule_count];
        database->instruction_schedule_count += 1;
        schedule->model_index = (u16)model_index;
        scrape_llvm_copy_name(schedule->instruction_name, instruction_name);
        scrape_llvm_copy_name(schedule->schedule_write_name, schedule_write_name);
        schedule->read_advance_count = (u16)BUSTER_MIN(read_advance_count, SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT_PER_SCHEDULE);
        for (u32 read_advance_i = 0; read_advance_i < schedule->read_advance_count; read_advance_i += 1)
        {
            scrape_llvm_copy_name(schedule->read_advance_names[read_advance_i], read_advance_names[read_advance_i]);
        }
    }
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_find_schedule_write_index(ScrapeLlvmDatabase* database, String8 name)
{
    u32 result = 0xffffffffu;
    for (u32 i = database->schedule_write_count; i > 0; i -= 1)
    {
        u32 index = i - 1;
        if (scrape_llvm_name_equals(database->schedule_writes[index].name, name))
        {
            result = index;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_find_schedule_write_index_for_model(ScrapeLlvmDatabase* database, u32 model_index, String8 name)
{
    u32 result = 0xffffffffu;
    for (u32 i = database->schedule_write_count; i > 0; i -= 1)
    {
        u32 index = i - 1;
        if (database->schedule_writes[index].model_index == model_index &&
            scrape_llvm_name_equals(database->schedule_writes[index].name, name))
        {
            result = index;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmScheduleWrite* scrape_llvm_schedule_write_by_index(ScrapeLlvmDatabase* database, u32 schedule_write_index)
{
    ScrapeLlvmScheduleWrite* result = 0;
    if (schedule_write_index != 0xffffffffu && schedule_write_index < database->schedule_write_count)
    {
        result = &database->schedule_writes[schedule_write_index];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmResolvedCost scrape_llvm_resolve_schedule_write_index(ScrapeLlvmDatabase* database, u32 schedule_write_index)
{
    ScrapeLlvmResolvedCost result = { 0 };
    ScrapeLlvmScheduleWrite* write = scrape_llvm_schedule_write_by_index(database, schedule_write_index);
    if (write)
    {
        if (write->has_metrics)
        {
            result = (ScrapeLlvmResolvedCost){
                .latency = write->latency,
                .micro_op_count = write->micro_op_count,
                .has_metrics = true,
            };
        }
        else if (write->primary_write_name[0] != 0)
        {
            u32 primary_index = 0xffffffffu;
            for (u32 candidate_i = 0; candidate_i < database->schedule_write_count; candidate_i += 1)
            {
                ScrapeLlvmScheduleWrite* candidate = &database->schedule_writes[candidate_i];
                if (candidate->model_index == write->model_index &&
                    scrape_llvm_name_equals(candidate->name, scrape_llvm_name_string8(write->primary_write_name)))
                {
                    primary_index = candidate_i;
                    break;
                }
            }
            ScrapeLlvmScheduleWrite* primary = scrape_llvm_schedule_write_by_index(database, primary_index);
            if (primary && primary->has_metrics)
            {
                result = (ScrapeLlvmResolvedCost){
                    .latency = primary->latency,
                    .micro_op_count = primary->micro_op_count,
                    .has_metrics = true,
                };
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmResolvedCost scrape_llvm_find_instruction_cost_for_model(ScrapeLlvmDatabase* database, u32 model_index, String8 instruction_name)
{
    ScrapeLlvmResolvedCost result = { 0 };
    for (u32 i = database->instruction_schedule_count; i > 0; i -= 1)
    {
        u32 index = i - 1;
        if (database->instruction_schedules[index].model_index == model_index &&
            scrape_llvm_name_equals(database->instruction_schedules[index].instruction_name, instruction_name))
        {
            for (u32 write_i = database->schedule_write_count; write_i > 0; write_i -= 1)
            {
                u32 schedule_write_index = write_i - 1;
                ScrapeLlvmScheduleWrite* write = &database->schedule_writes[schedule_write_index];
                if (write->model_index == model_index &&
                    scrape_llvm_name_equals(write->name, scrape_llvm_name_string8(database->instruction_schedules[index].schedule_write_name)))
                {
                    result = scrape_llvm_resolve_schedule_write_index(database, schedule_write_index);
                    break;
                }
            }
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmResolvedCost scrape_llvm_find_schedule_write_cost_for_model(ScrapeLlvmDatabase* database, u32 model_index, String8 schedule_write_name)
{
    ScrapeLlvmResolvedCost result = { 0 };
    for (u32 i = database->schedule_write_count; i > 0; i -= 1)
    {
        u32 index = i - 1;
        ScrapeLlvmScheduleWrite* write = &database->schedule_writes[index];
        if (write->model_index == model_index && scrape_llvm_name_equals(write->name, schedule_write_name))
        {
            result = scrape_llvm_resolve_schedule_write_index(database, index);
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ScrapeLlvmResolvedCost scrape_llvm_resolve_lowering_cost(ScrapeLlvmDatabase* database, ScrapeLlvmLoweringCostSpec spec)
{
    ScrapeLlvmResolvedCost result = { 0 };
    u32 preferred_model_index = scrape_llvm_find_processor_model_index(database, scrape_llvm_preferred_model_name());

    for (u32 instruction_i = 0; !result.has_metrics && instruction_i < spec.instruction_name_count; instruction_i += 1)
    {
        if (preferred_model_index != 0xffffffffu)
        {
            result = scrape_llvm_find_instruction_cost_for_model(database, preferred_model_index, spec.instruction_names[instruction_i]);
        }
        if (!result.has_metrics)
        {
            for (u32 model_i = 0; !result.has_metrics && model_i < database->processor_model_count; model_i += 1)
            {
                result = scrape_llvm_find_instruction_cost_for_model(database, model_i, spec.instruction_names[instruction_i]);
            }
        }
    }

    for (u32 write_i = 0; !result.has_metrics && write_i < spec.schedule_write_name_count; write_i += 1)
    {
        if (preferred_model_index != 0xffffffffu)
        {
            result = scrape_llvm_find_schedule_write_cost_for_model(database, preferred_model_index, spec.schedule_write_names[write_i]);
        }
        if (!result.has_metrics)
        {
            for (u32 model_i = 0; !result.has_metrics && model_i < database->processor_model_count; model_i += 1)
            {
                result = scrape_llvm_find_schedule_write_cost_for_model(database, model_i, spec.schedule_write_names[write_i]);
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_import_tblgen_instructions(ScrapeLlvmDatabase* database, ScrapeLlvmJsonValue* instanceof_value)
{
    ScrapeLlvmJsonValue* instruction_names = scrape_llvm_json_object_find(instanceof_value, S8("Instruction"));
    if (instruction_names && instruction_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = instruction_names->array_value.first; item; item = item->next)
        {
            String8 instruction_name = scrape_llvm_json_string(item->value);
            scrape_llvm_add_instruction(database, instruction_name, false);
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_import_tblgen_processor_models(ScrapeLlvmDatabase* database, ScrapeLlvmJsonValue* instanceof_value, ScrapeLlvmRecordMap record_map)
{
    ScrapeLlvmJsonValue* model_names = scrape_llvm_json_object_find(instanceof_value, S8("SchedMachineModel"));
    if (model_names && model_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = model_names->array_value.first; item; item = item->next)
        {
            String8 model_name = scrape_llvm_json_string(item->value);
            ScrapeLlvmJsonValue* model_record = scrape_llvm_record_map_find(record_map, model_name);
            u32 model_index = scrape_llvm_add_processor_model(database, model_name);
            if (model_record && model_index != 0xffffffffu)
            {
                ScrapeLlvmProcessorModel* model = &database->processor_models[model_index];
                model->issue_width = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("IssueWidth")), 0), 0);
                model->micro_op_buffer_size = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("MicroOpBufferSize")), 0), 0);
                model->loop_micro_op_buffer_size = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("LoopMicroOpBufferSize")), 0), 0);
                model->load_latency = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("LoadLatency")), 0), 0);
                model->vector_load_latency = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("VecLoadLatency")), 0), 0);
                model->store_latency = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("StoreLatency")), 0), 0);
                model->high_latency = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("HighLatency")), 0), 0);
                model->mispredict_penalty = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("MispredictPenalty")), 0), 0);
                model->complete_model = scrape_llvm_json_integer(scrape_llvm_json_object_find(model_record, S8("CompleteModel")), 0) != 0;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_import_tblgen_processor_resources(ScrapeLlvmDatabase* database, ScrapeLlvmJsonValue* instanceof_value, ScrapeLlvmRecordMap record_map)
{
    String8 class_names[] = {
        S8("ProcResource"),
        S8("ProcResGroup"),
    };

    for (u32 class_i = 0; class_i < BUSTER_ARRAY_LENGTH(class_names); class_i += 1)
    {
        ScrapeLlvmJsonValue* resource_names = scrape_llvm_json_object_find(instanceof_value, class_names[class_i]);
        if (resource_names && resource_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
        {
            for (ScrapeLlvmJsonArrayItem* item = resource_names->array_value.first; item; item = item->next)
            {
                String8 resource_name = scrape_llvm_json_string(item->value);
                ScrapeLlvmJsonValue* resource_record = scrape_llvm_record_map_find(record_map, resource_name);
                String8 model_name = scrape_llvm_json_def_name(scrape_llvm_json_object_find(resource_record, S8("SchedModel")));
                u32 model_index = scrape_llvm_find_processor_model_index(database, model_name);
                u32 units = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(resource_record, S8("NumUnits")), 0), 0);
                s64 buffer_size_signed = scrape_llvm_json_integer(scrape_llvm_json_object_find(resource_record, S8("BufferSize")), 0);
                u32 buffer_size = (u32)BUSTER_MAX(buffer_size_signed, 0);
                String8 member_names[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_GROUP] = { 0 };
                u32 member_count = 0;
                bool is_group = string_equal(class_names[class_i], S8("ProcResGroup"));

                if (is_group)
                {
                    scrape_llvm_json_array_collect_def_names(
                        scrape_llvm_json_object_find(resource_record, S8("Resources")),
                        member_names,
                        BUSTER_ARRAY_LENGTH(member_names),
                        &member_count);
                }

                scrape_llvm_add_processor_resource(database, model_index, resource_name, is_group, units, buffer_size, member_names, member_count);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_import_tblgen_read_advances(ScrapeLlvmDatabase* database, ScrapeLlvmJsonValue* instanceof_value, ScrapeLlvmRecordMap record_map)
{
    ScrapeLlvmJsonValue* read_advance_names = scrape_llvm_json_object_find(instanceof_value, S8("ReadAdvance"));
    if (read_advance_names && read_advance_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = read_advance_names->array_value.first; item; item = item->next)
        {
            String8 read_advance_record_name = scrape_llvm_json_string(item->value);
            ScrapeLlvmJsonValue* read_advance_record = scrape_llvm_record_map_find(record_map, read_advance_record_name);
            String8 model_name = scrape_llvm_json_def_name(scrape_llvm_json_object_find(read_advance_record, S8("SchedModel")));
            String8 read_type_name = scrape_llvm_json_def_name(scrape_llvm_json_object_find(read_advance_record, S8("ReadType")));
            s32 cycles = (s32)scrape_llvm_json_integer(scrape_llvm_json_object_find(read_advance_record, S8("Cycles")), 0);
            u32 model_index = scrape_llvm_find_processor_model_index(database, model_name);
            scrape_llvm_add_read_advance(database, model_index, read_type_name, cycles);
        }
    }
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_tblgen_resolve_variant_primary_write(ScrapeLlvmRecordMap record_map, ScrapeLlvmJsonValue* variant_record)
{
    String8 result = { 0 };
    ScrapeLlvmJsonValue* variants = scrape_llvm_json_object_find(variant_record, S8("Variants"));
    if (variants && variants->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = variants->array_value.first; item; item = item->next)
        {
            String8 variant_name = scrape_llvm_json_def_name(item->value);
            ScrapeLlvmJsonValue* sched_var_record = scrape_llvm_record_map_find(record_map, variant_name);
            String8 predicate_name = scrape_llvm_json_def_name(scrape_llvm_json_object_find(sched_var_record, S8("Predicate")));
            ScrapeLlvmJsonValue* selected = scrape_llvm_json_object_find(sched_var_record, S8("Selected"));
            String8 selected_name = { 0 };
            if (selected && selected->kind == SCRAPE_LLVM_JSON_KIND_ARRAY && selected->array_value.first)
            {
                selected_name = scrape_llvm_json_def_name(selected->array_value.first->value);
            }

            if (selected_name.length > 0)
            {
                result = selected_name;
                if (string_equal(predicate_name, S8("NoSchedPred")))
                {
                    break;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_import_tblgen_schedule_writes(ScrapeLlvmDatabase* database, ScrapeLlvmJsonValue* instanceof_value, ScrapeLlvmRecordMap record_map)
{
    u32 preferred_model_index = scrape_llvm_find_processor_model_index(database, scrape_llvm_preferred_model_name());
    ScrapeLlvmJsonValue* write_res_names = scrape_llvm_json_object_find(instanceof_value, S8("SchedWriteRes"));
    if (write_res_names && write_res_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = write_res_names->array_value.first; item; item = item->next)
        {
            String8 write_name = scrape_llvm_json_string(item->value);
            ScrapeLlvmJsonValue* write_record = scrape_llvm_record_map_find(record_map, write_name);
            String8 model_name = scrape_llvm_json_def_name(scrape_llvm_json_object_find(write_record, S8("SchedModel")));
            u32 model_index = scrape_llvm_find_processor_model_index(database, model_name);
            u32 latency = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(write_record, S8("Latency")), 0), 0);
            u32 micro_op_count = (u32)BUSTER_MAX(scrape_llvm_json_integer(scrape_llvm_json_object_find(write_record, S8("NumMicroOps")), 0), 0);
            String8 resource_names[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE] = { 0 };
            u32 release_cycles[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE] = { 0 };
            u32 resource_name_count = 0;

            scrape_llvm_json_array_collect_def_names(
                scrape_llvm_json_object_find(write_record, S8("ProcResources")),
                resource_names,
                BUSTER_ARRAY_LENGTH(resource_names),
                &resource_name_count);

            ScrapeLlvmJsonValue* release_at_cycles = scrape_llvm_json_object_find(write_record, S8("ReleaseAtCycles"));
            if (release_at_cycles && release_at_cycles->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
            {
                u32 release_i = 0;
                for (ScrapeLlvmJsonArrayItem* release_item = release_at_cycles->array_value.first;
                     release_item && release_i < resource_name_count;
                     release_item = release_item->next, release_i += 1)
                {
                    release_cycles[release_i] = (u32)BUSTER_MAX(scrape_llvm_json_integer(release_item->value, 0), 0);
                }
            }

            scrape_llvm_add_schedule_write(database, model_index, write_name, latency, micro_op_count, latency != 0 || micro_op_count != 0, false, (String8){ 0 }, resource_names, release_cycles, resource_name_count);
        }
    }

    ScrapeLlvmJsonValue* variant_names = scrape_llvm_json_object_find(instanceof_value, S8("SchedWriteVariant"));
    if (variant_names && variant_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = variant_names->array_value.first; item; item = item->next)
        {
            String8 write_name = scrape_llvm_json_string(item->value);
            ScrapeLlvmJsonValue* write_record = scrape_llvm_record_map_find(record_map, write_name);
            String8 model_name = scrape_llvm_json_def_name(scrape_llvm_json_object_find(write_record, S8("SchedModel")));
            u32 model_index = scrape_llvm_find_processor_model_index(database, model_name);
            String8 primary_write_name = scrape_llvm_tblgen_resolve_variant_primary_write(record_map, write_record);
            scrape_llvm_add_schedule_write(database, model_index, write_name, 0, 0, false, true, primary_write_name, 0, 0, 0);
        }
    }

    ScrapeLlvmJsonValue* write_sequence_names = scrape_llvm_json_object_find(instanceof_value, S8("WriteSequence"));
    if (write_sequence_names && write_sequence_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = write_sequence_names->array_value.first; item; item = item->next)
        {
            String8 write_name = scrape_llvm_json_string(item->value);
            ScrapeLlvmJsonValue* write_record = scrape_llvm_record_map_find(record_map, write_name);
            ScrapeLlvmJsonValue* writes = scrape_llvm_json_object_find(write_record, S8("Writes"));
            String8 primary_write_name = { 0 };
            if (writes && writes->kind == SCRAPE_LLVM_JSON_KIND_ARRAY && writes->array_value.first)
            {
                primary_write_name = scrape_llvm_json_def_name(writes->array_value.first->value);
            }
            if (primary_write_name.length > 0)
            {
                scrape_llvm_add_schedule_write(database, preferred_model_index, write_name, 0, 0, false, true, primary_write_name, 0, 0, 0);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_import_tblgen_instrw_entries(ScrapeLlvmDatabase* database, ScrapeLlvmJsonValue* instanceof_value, ScrapeLlvmRecordMap record_map)
{
    ScrapeLlvmJsonValue* instrw_names = scrape_llvm_json_object_find(instanceof_value, S8("InstRW"));
    if (instrw_names && instrw_names->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
    {
        for (ScrapeLlvmJsonArrayItem* item = instrw_names->array_value.first; item; item = item->next)
        {
            String8 instrw_name = scrape_llvm_json_string(item->value);
            ScrapeLlvmJsonValue* instrw_record = scrape_llvm_record_map_find(record_map, instrw_name);
            String8 model_name = scrape_llvm_json_def_name(scrape_llvm_json_object_find(instrw_record, S8("SchedModel")));
            u32 model_index = scrape_llvm_find_processor_model_index(database, model_name);
            String8 list_names[1 + SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT_PER_SCHEDULE] = { 0 };
            u32 list_name_count = 0;
            scrape_llvm_json_array_collect_def_names(
                scrape_llvm_json_object_find(instrw_record, S8("OperandReadWrites")),
                list_names,
                BUSTER_ARRAY_LENGTH(list_names),
                &list_name_count);

            String8 schedule_write_name = list_name_count > 0 ? list_names[0] : (String8){ 0 };
            String8 read_advance_names[SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT_PER_SCHEDULE] = { 0 };
            u32 read_advance_count = 0;
            for (u32 list_name_i = 1; list_name_i < list_name_count && read_advance_count < BUSTER_ARRAY_LENGTH(read_advance_names); list_name_i += 1)
            {
                read_advance_names[read_advance_count] = list_names[list_name_i];
                read_advance_count += 1;
            }

            ScrapeLlvmJsonValue* instrs = scrape_llvm_json_object_find(instrw_record, S8("Instrs"));
            ScrapeLlvmJsonValue* instrs_operator = scrape_llvm_json_object_find(instrs, S8("operator"));
            String8 operator_name = scrape_llvm_json_def_name(instrs_operator);
            ScrapeLlvmJsonValue* args = scrape_llvm_json_object_find(instrs, S8("args"));
            if (args && args->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
            {
                if (string_equal(operator_name, S8("instrs")))
                {
                    for (ScrapeLlvmJsonArrayItem* arg_item = args->array_value.first; arg_item; arg_item = arg_item->next)
                    {
                        ScrapeLlvmJsonValue* arg_pair = arg_item->value;
                        String8 instruction_name = { 0 };
                        if (arg_pair && arg_pair->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
                        {
                            instruction_name = scrape_llvm_json_def_name(scrape_llvm_json_array_at(arg_pair, 0));
                        }
                        if (instruction_name.length > 0)
                        {
                            scrape_llvm_add_instruction_schedule(database, model_index, instruction_name, schedule_write_name, read_advance_names, read_advance_count);
                        }
                    }
                }
#if !defined(_WIN32)
                else if (string_equal(operator_name, S8("instregex")))
                {
                    for (ScrapeLlvmJsonArrayItem* arg_item = args->array_value.first; arg_item; arg_item = arg_item->next)
                    {
                        ScrapeLlvmJsonValue* arg_pair = arg_item->value;
                        String8 regex_pattern = { 0 };
                        if (arg_pair && arg_pair->kind == SCRAPE_LLVM_JSON_KIND_ARRAY)
                        {
                            regex_pattern = scrape_llvm_json_string(scrape_llvm_json_array_at(arg_pair, 0));
                        }
                        if (regex_pattern.length > 0)
                        {
                            scrape_llvm_add_instruction_schedules_for_regex(database, model_index, regex_pattern, schedule_write_name, read_advance_names, read_advance_count);
                        }
                    }
                }
#endif
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_build_database_from_tblgen_json(Arena* arena, ScrapeLlvmDatabase* database, StringOs llvm_root, StringOs llvm_tblgen_path)
{
    bool result = false;
    ByteSlice json_dump = scrape_llvm_run_tblgen_dump_json(arena, llvm_root, llvm_tblgen_path);
    if (json_dump.length != 0)
    {
        ScrapeLlvmJsonParser parser = {
            .arena = arena,
            .text = BYTE_SLICE_TO_STRING(8, json_dump),
        };
        ScrapeLlvmJsonValue* root = scrape_llvm_json_parse_value(&parser);
        ScrapeLlvmJsonValue* instanceof_value = scrape_llvm_json_object_find(root, S8("!instanceof"));
        ScrapeLlvmRecordMap record_map = scrape_llvm_build_record_map(arena, root);

        if (root && instanceof_value)
        {
            scrape_llvm_import_tblgen_instructions(database, instanceof_value);
            scrape_llvm_import_tblgen_processor_models(database, instanceof_value, record_map);
            scrape_llvm_import_tblgen_processor_resources(database, instanceof_value, record_map);
            scrape_llvm_import_tblgen_read_advances(database, instanceof_value, record_map);
            scrape_llvm_import_tblgen_schedule_writes(database, instanceof_value, record_map);
            scrape_llvm_import_tblgen_instrw_entries(database, instanceof_value, record_map);
            result = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_collect_unique_scheduled_instruction_indices(ScrapeLlvmDatabase* database, u32* out_indices, u32 out_index_capacity)
{
    u32 result = 0;

    for (u32 instruction_schedule_i = 0; instruction_schedule_i < database->instruction_schedule_count; instruction_schedule_i += 1)
    {
        String8 name = scrape_llvm_name_string8(database->instruction_schedules[instruction_schedule_i].instruction_name);
        bool already_seen = false;
        for (u32 existing_i = 0; existing_i < result; existing_i += 1)
        {
            u32 existing_index = out_indices[existing_i];
            String8 existing_name = scrape_llvm_name_string8(database->instruction_schedules[existing_index].instruction_name);
            if (string_equal(existing_name, name))
            {
                already_seen = true;
                break;
            }
        }

        if (!already_seen && result < out_index_capacity)
        {
            out_indices[result] = instruction_schedule_i;
            result += 1;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_collect_identifiers(String8 text, String8* out_names, u32 out_capacity)
{
    u32 result = 0;
    u64 cursor = 0;
    while (result < out_capacity)
    {
        String8 name = scrape_llvm_next_identifier_or_sign(text, &cursor);
        if (name.length == 0)
        {
            break;
        }
        out_names[result] = name;
        result += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_llvm_first_model_name_in_file(String8 text)
{
    String8 result = { 0 };
    u64 match = string8_first_sequence(text, S8("let SchedModel = "));
    if (match != BUSTER_STRING_NO_MATCH)
    {
        String8 remainder = string8_from_pointer_length(
            text.pointer + match + BUSTER_COMPILE_TIME_STRING_LENGTH("let SchedModel = "),
            text.length - match - BUSTER_COMPILE_TIME_STRING_LENGTH("let SchedModel = "));
        result = scrape_llvm_first_identifier(remainder);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_definition_line(ScrapeLlvmDatabase* database, String8 line)
{
    String8 trimmed = scrape_llvm_trim(line);
    if (string8_starts_with_sequence(trimmed, S8("def ")))
    {
        scrape_llvm_add_instruction(database, scrape_llvm_identifier_after_keyword(trimmed, S8("def ")), false);
    }
    else if (string8_starts_with_sequence(trimmed, S8("defm ")))
    {
        scrape_llvm_add_instruction(database, scrape_llvm_identifier_after_keyword(trimmed, S8("defm ")), true);
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_model_defs(ScrapeLlvmDatabase* database, String8 text)
{
    u64 cursor = 0;
    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 match = string8_first_sequence(remaining, S8(": SchedMachineModel"));
        if (match == BUSTER_STRING_NO_MATCH)
        {
            break;
        }

        u64 global_match = cursor + match;
        u64 line_start = global_match;
        while (line_start > 0 && text.pointer[line_start - 1] != '\n')
        {
            line_start -= 1;
        }

        String8 prefix = string8_from_pointer_length(text.pointer + line_start, global_match - line_start);
        String8 model_name = scrape_llvm_identifier_after_keyword(prefix, S8("def "));
        u32 model_index = scrape_llvm_add_processor_model(database, model_name);

        u64 block_start = global_match;
        while (block_start < text.length && text.pointer[block_start] != '{')
        {
            block_start += 1;
        }
        if (block_start >= text.length || text.pointer[block_start] != '{')
        {
            cursor = global_match + 1;
            continue;
        }

        u64 block_end = scrape_llvm_find_matching_character(text, block_start, '{', '}');
        if (block_end == BUSTER_STRING_NO_MATCH)
        {
            cursor = global_match + 1;
            continue;
        }

        String8 segment = string8_from_pointer_length(text.pointer + block_start, block_end - block_start + 1);
        ScrapeLlvmProcessorModel* model = &database->processor_models[model_index];
        u32 value = 0;
        if (scrape_llvm_extract_u32_after(segment, S8("IssueWidth = "), &value)) model->issue_width = value;
        if (scrape_llvm_extract_u32_after(segment, S8("MicroOpBufferSize = "), &value)) model->micro_op_buffer_size = value;
        if (scrape_llvm_extract_u32_after(segment, S8("LoopMicroOpBufferSize = "), &value)) model->loop_micro_op_buffer_size = value;
        if (scrape_llvm_extract_u32_after(segment, S8("LoadLatency = "), &value)) model->load_latency = value;
        if (scrape_llvm_extract_u32_after(segment, S8("VecLoadLatency = "), &value)) model->vector_load_latency = value;
        if (scrape_llvm_extract_u32_after(segment, S8("StoreLatency = "), &value)) model->store_latency = value;
        if (scrape_llvm_extract_u32_after(segment, S8("HighLatency = "), &value)) model->high_latency = value;
        if (scrape_llvm_extract_u32_after(segment, S8("MispredictPenalty = "), &value)) model->mispredict_penalty = value;
        if (scrape_llvm_extract_u32_after(segment, S8("CompleteModel = "), &value)) model->complete_model = value != 0;

        cursor = block_end + 1;
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_processor_resources(ScrapeLlvmDatabase* database, String8 text, u32 model_index)
{
    u64 cursor = 0;
    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 proc_resource_match = string8_first_sequence(remaining, S8(": ProcResource<"));
        u64 proc_group_match = string8_first_sequence(remaining, S8(": ProcResGroup<["));
        bool is_group = false;
        u64 match = proc_resource_match;
        if (proc_group_match != BUSTER_STRING_NO_MATCH && (match == BUSTER_STRING_NO_MATCH || proc_group_match < match))
        {
            match = proc_group_match;
            is_group = true;
        }
        if (match == BUSTER_STRING_NO_MATCH)
        {
            break;
        }

        u64 global_match = cursor + match;
        u64 line_start = global_match;
        while (line_start > 0 && text.pointer[line_start - 1] != '\n')
        {
            line_start -= 1;
        }
        String8 prefix = string8_from_pointer_length(text.pointer + line_start, global_match - line_start);
        String8 name = scrape_llvm_identifier_after_keyword(prefix, S8("def "));
        if (name.length == 0)
        {
            cursor = global_match + 1;
            continue;
        }

        u64 block_start = global_match;
        while (block_start < text.length && text.pointer[block_start] != '{' && text.pointer[block_start] != ';')
        {
            block_start += 1;
        }
        u64 segment_end = block_start;
        if (block_start < text.length && text.pointer[block_start] == '{')
        {
            u64 block_end = scrape_llvm_find_matching_character(text, block_start, '{', '}');
            if (block_end != BUSTER_STRING_NO_MATCH)
            {
                segment_end = block_end;
            }
        }
        String8 segment = string8_from_pointer_length(text.pointer + global_match, segment_end - global_match + 1);

        u32 units = 0;
        u32 buffer_size = 0;
        String8 member_names[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_GROUP] = { 0 };
        u32 member_count = 0;
        if (is_group)
        {
            u64 bracket_start = string8_first_sequence(segment, S8("["));
            if (bracket_start != BUSTER_STRING_NO_MATCH)
            {
                u64 bracket_end = scrape_llvm_find_matching_character(segment, bracket_start, '[', ']');
                if (bracket_end != BUSTER_STRING_NO_MATCH)
                {
                    String8 members = string8_from_pointer_length(segment.pointer + bracket_start + 1, bracket_end - bracket_start - 1);
                    member_count = scrape_llvm_collect_identifiers(members, member_names, BUSTER_ARRAY_LENGTH(member_names));
                }
            }
        }
        else
        {
            u64 angle_start = string8_first_sequence(segment, S8("<"));
            u64 angle_end = string8_first_sequence(segment, S8(">"));
            if (angle_start != BUSTER_STRING_NO_MATCH && angle_end != BUSTER_STRING_NO_MATCH && angle_end > angle_start)
            {
                String8 units_text = scrape_llvm_trim(string8_from_pointer_length(segment.pointer + angle_start + 1, angle_end - angle_start - 1));
                scrape_llvm_resolve_model_u32(database, model_index, units_text, &units);
            }
        }
        scrape_llvm_extract_u32_after(segment, S8("BufferSize = "), &buffer_size);
        scrape_llvm_add_processor_resource(database, model_index, name, is_group, units, buffer_size, member_names, member_count);

        cursor = global_match + 1;
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_read_advances(ScrapeLlvmDatabase* database, String8 text, u32 model_index)
{
    u64 cursor = 0;
    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 match = string8_first_sequence(remaining, S8("ReadAdvance<"));
        if (match == BUSTER_STRING_NO_MATCH)
        {
            break;
        }

        u64 global_match = cursor + match + BUSTER_COMPILE_TIME_STRING_LENGTH("ReadAdvance<");
        u64 angle_end = global_match;
        while (angle_end < text.length && text.pointer[angle_end] != '>')
        {
            angle_end += 1;
        }
        String8 args = string8_from_pointer_length(text.pointer + global_match, angle_end - global_match);
        u64 args_cursor = 0;
        String8 read_name = scrape_llvm_next_identifier_or_sign(args, &args_cursor);
        String8 cycles_text = scrape_llvm_next_identifier_or_sign(args, &args_cursor);
        s32 cycles = 0;
        if (read_name.length > 0 && scrape_llvm_parse_s32(cycles_text, &cycles))
        {
            scrape_llvm_add_read_advance(database, model_index, read_name, cycles);
        }
        else if (read_name.length > 0)
        {
            u32 resolved_cycles = 0;
            if (scrape_llvm_resolve_model_u32(database, model_index, cycles_text, &resolved_cycles))
            {
                scrape_llvm_add_read_advance(database, model_index, read_name, (s32)resolved_cycles);
            }
        }

        cursor = global_match + 1;
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_schedwrite_defs(ScrapeLlvmDatabase* database, String8 text, u32 model_index)
{
    u64 cursor = 0;
    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 match = string8_first_sequence(remaining, S8(": SchedWrite"));
        if (match == BUSTER_STRING_NO_MATCH)
        {
            break;
        }

        u64 global_match = cursor + match;
        u64 line_start = global_match;
        while (line_start > 0 && text.pointer[line_start - 1] != '\n')
        {
            line_start -= 1;
        }

        String8 prefix = string8_from_pointer_length(text.pointer + line_start, global_match - line_start);
        String8 name = { 0 };
        if (string8_first_sequence(prefix, S8("def ")) != BUSTER_STRING_NO_MATCH)
        {
            name = scrape_llvm_identifier_after_keyword(prefix, S8("def "));
        }
        else if (string8_first_sequence(prefix, S8("defm ")) != BUSTER_STRING_NO_MATCH)
        {
            name = scrape_llvm_identifier_after_keyword(prefix, S8("defm "));
        }

        u64 statement_end = string8_first_sequence(string8_from_pointer_length(text.pointer + global_match, text.length - global_match), S8(";"));
        if (statement_end == BUSTER_STRING_NO_MATCH)
        {
            break;
        }
        statement_end = global_match + statement_end;

        u64 block_start = global_match;
        while (block_start < text.length && text.pointer[block_start] != '{' && text.pointer[block_start] != ';')
        {
            block_start += 1;
        }

        u64 segment_end = statement_end;
        if (block_start < text.length && text.pointer[block_start] == '{')
        {
            u64 block_end = scrape_llvm_find_matching_character(text, block_start, '{', '}');
            if (block_end != BUSTER_STRING_NO_MATCH && block_end > segment_end)
            {
                segment_end = block_end;
            }
        }

        String8 segment = string8_from_pointer_length(text.pointer + global_match, segment_end - global_match + 1);
        bool is_variant = string8_first_sequence(segment, S8("SchedWriteVariant<")) != BUSTER_STRING_NO_MATCH;
        u32 latency = 0;
        u32 micro_op_count = 0;
        bool has_metrics = false;
        has_metrics |= scrape_llvm_extract_u32_after(segment, S8("Latency = "), &latency);
        has_metrics |= scrape_llvm_extract_u32_after(segment, S8("NumMicroOps = "), &micro_op_count);
        String8 resource_names[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE] = { 0 };
        u32 release_cycles[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE] = { 0 };
        u32 resource_name_count = 0;

        String8 primary_write_name = { 0 };
        if (is_variant)
        {
            u64 first_bracket = string8_first_sequence(segment, S8("["));
            if (first_bracket != BUSTER_STRING_NO_MATCH)
            {
                String8 write_segment = string8_from_pointer_length(segment.pointer + first_bracket + 1, segment.length - first_bracket - 1);
                primary_write_name = scrape_llvm_first_identifier(write_segment);
            }
        }
        else
        {
            u64 bracket_start = string8_first_sequence(segment, S8("["));
            if (bracket_start != BUSTER_STRING_NO_MATCH)
            {
                u64 bracket_end = scrape_llvm_find_matching_character(segment, bracket_start, '[', ']');
                if (bracket_end != BUSTER_STRING_NO_MATCH)
                {
                    String8 resources = string8_from_pointer_length(segment.pointer + bracket_start + 1, bracket_end - bracket_start - 1);
                    resource_name_count = scrape_llvm_collect_identifiers(resources, resource_names, BUSTER_ARRAY_LENGTH(resource_names));
                }
            }

            u64 release_match = string8_first_sequence(segment, S8("ReleaseAtCycles = ["));
            if (release_match != BUSTER_STRING_NO_MATCH)
            {
                u64 release_start = release_match + BUSTER_COMPILE_TIME_STRING_LENGTH("ReleaseAtCycles = [");
                u64 release_end = scrape_llvm_find_matching_character(segment, release_start - 1, '[', ']');
                if (release_end != BUSTER_STRING_NO_MATCH)
                {
                    String8 release_text = string8_from_pointer_length(segment.pointer + release_start, release_end - release_start);
                    u64 release_cursor = 0;
                    for (u32 release_i = 0; release_i < resource_name_count; release_i += 1)
                    {
                        String8 number_text = scrape_llvm_next_identifier_or_sign(release_text, &release_cursor);
                        u32 value = 0;
                        if (scrape_llvm_resolve_model_u32(database, model_index, number_text, &value))
                        {
                            release_cycles[release_i] = value;
                        }
                    }
                }
            }
        }

        scrape_llvm_add_schedule_write(database, model_index, name, latency, micro_op_count, has_metrics, is_variant, primary_write_name, resource_names, release_cycles, resource_name_count);
        cursor = global_match + 1;
    }

    cursor = 0;
    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 line_end = string8_first_sequence(remaining, S8("\n"));
        if (line_end == BUSTER_STRING_NO_MATCH)
        {
            line_end = remaining.length;
        }

        String8 line = string8_from_pointer_length(remaining.pointer, line_end);
        String8 trimmed = scrape_llvm_trim(line);
        if (string8_starts_with_sequence(trimmed, S8("defm ")))
        {
            String8 named_write = scrape_llvm_identifier_after_keyword(trimmed, S8("defm "));
            if (string8_starts_with_sequence(named_write, S8("Write")))
            {
                scrape_llvm_add_schedule_write(database, model_index, named_write, 0, 0, false, false, (String8){ 0 }, 0, 0, 0);
            }
            else if (string8_starts_with_sequence(trimmed, S8("defm :")))
            {
                u64 angle_start = string8_first_sequence(trimmed, S8("<"));
                u64 angle_end = string8_first_sequence(trimmed, S8(">"));
                if (angle_start != BUSTER_STRING_NO_MATCH && angle_end != BUSTER_STRING_NO_MATCH && angle_end > angle_start)
                {
                    String8 args = string8_from_pointer_length(trimmed.pointer + angle_start + 1, angle_end - angle_start - 1);
                    String8 write_name = scrape_llvm_first_identifier(args);
                    if (string8_starts_with_sequence(write_name, S8("Write")))
                    {
                        u32 numbers[8] = { 0 };
                        u32 number_count = 0;
                        for (u64 char_i = 0; char_i < args.length && number_count < BUSTER_ARRAY_LENGTH(numbers); char_i += 1)
                        {
                            if (scrape_llvm_ascii_is_digit(args.pointer[char_i]))
                            {
                                u32 value = 0;
                                while (char_i < args.length && scrape_llvm_ascii_is_digit(args.pointer[char_i]))
                                {
                                    value = value * 10 + (u32)(args.pointer[char_i] - '0');
                                    char_i += 1;
                                }
                                numbers[number_count] = value;
                                number_count += 1;
                            }
                        }

                        u32 parsed_latency = number_count >= 1 ? numbers[0] : 0;
                        u32 parsed_micro_op_count = number_count >= 2 ? numbers[number_count - 1] : 0;
                        bool parsed_has_metrics = number_count >= 2;
                        scrape_llvm_add_schedule_write(database, model_index, write_name, parsed_latency, parsed_micro_op_count, parsed_has_metrics, false, (String8){ 0 }, 0, 0, 0);
                    }
                }
            }
        }

        if (string8_starts_with_sequence(trimmed, S8("defm : X86WriteRes<")))
        {
            u64 angle_start = string8_first_sequence(trimmed, S8("<"));
            u64 angle_end = string8_first_sequence(trimmed, S8(">"));
            if (angle_start != BUSTER_STRING_NO_MATCH && angle_end != BUSTER_STRING_NO_MATCH && angle_end > angle_start)
            {
                String8 args = string8_from_pointer_length(trimmed.pointer + angle_start + 1, angle_end - angle_start - 1);
                String8 fields[8] = { 0 };
                u32 field_count = scrape_llvm_collect_identifiers(args, fields, BUSTER_ARRAY_LENGTH(fields));
                String8 resource_names[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE] = { 0 };
                u32 release_cycles[SCRAPE_LLVM_MAX_RESOURCE_NAME_COUNT_PER_WRITE] = { 0 };
                u32 resource_name_count = 0;
                u32 parsed_latency = 0;
                u32 parsed_micro_op_count = 0;
                bool parsed_has_metrics = false;
                if (field_count >= 1)
                {
                    u64 bracket_start = string8_first_sequence(args, S8("["));
                    if (bracket_start != BUSTER_STRING_NO_MATCH)
                    {
                        u64 bracket_end = scrape_llvm_find_matching_character(args, bracket_start, '[', ']');
                        if (bracket_end != BUSTER_STRING_NO_MATCH)
                        {
                            String8 resources = string8_from_pointer_length(args.pointer + bracket_start + 1, bracket_end - bracket_start - 1);
                            resource_name_count = scrape_llvm_collect_identifiers(resources, resource_names, BUSTER_ARRAY_LENGTH(resource_names));
                        }
                    }

                    u32 numbers[32] = { 0 };
                    u32 number_count = 0;
                    for (u64 args_i = 0; args_i < args.length && number_count < BUSTER_ARRAY_LENGTH(numbers); args_i += 1)
                    {
                        if (scrape_llvm_ascii_is_digit(args.pointer[args_i]))
                        {
                            u32 number = 0;
                            while (args_i < args.length && scrape_llvm_ascii_is_digit(args.pointer[args_i]))
                            {
                                number = number * 10 + (u32)(args.pointer[args_i] - '0');
                                args_i += 1;
                            }
                            numbers[number_count] = number;
                            number_count += 1;
                        }
                    }
                    if (number_count >= 2)
                    {
                        parsed_latency = numbers[0];
                        parsed_micro_op_count = numbers[number_count - 1];
                        parsed_has_metrics = true;
                        for (u32 number_i = 1; number_i + 1 < number_count && number_i - 1 < resource_name_count; number_i += 1)
                        {
                            release_cycles[number_i - 1] = numbers[number_i];
                        }
                    }

                    scrape_llvm_add_schedule_write(database, model_index, fields[0], parsed_latency, parsed_micro_op_count, parsed_has_metrics, false, (String8){ 0 }, resource_names, release_cycles, resource_name_count);
                }
            }
        }

        cursor += line_end;
        if (cursor < text.length && text.pointer[cursor] == '\n')
        {
            cursor += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_instrw_entries(ScrapeLlvmDatabase* database, String8 text, u32 model_index)
{
    u64 cursor = 0;
    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 match = string8_first_sequence(remaining, S8("InstRW<["));
        if (match == BUSTER_STRING_NO_MATCH)
        {
            break;
        }

        u64 global_match = cursor + match;
        u64 write_start = global_match + BUSTER_COMPILE_TIME_STRING_LENGTH("InstRW<[");
        u64 write_end = write_start;
        while (write_end < text.length && text.pointer[write_end] != ']')
        {
            write_end += 1;
        }

        String8 write_list = string8_from_pointer_length(text.pointer + write_start, write_end - write_start);
        String8 list_names[1 + SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT_PER_SCHEDULE] = { 0 };
        u32 list_name_count = scrape_llvm_collect_identifiers(write_list, list_names, BUSTER_ARRAY_LENGTH(list_names));
        String8 schedule_write_name = list_name_count > 0 ? list_names[0] : (String8){ 0 };
        String8 read_advance_names[SCRAPE_LLVM_MAX_READ_ADVANCE_COUNT_PER_SCHEDULE] = { 0 };
        u32 read_advance_count = 0;
        for (u32 list_name_i = 1; list_name_i < list_name_count && read_advance_count < BUSTER_ARRAY_LENGTH(read_advance_names); list_name_i += 1)
        {
            read_advance_names[read_advance_count] = list_names[list_name_i];
            read_advance_count += 1;
        }

        u64 instrs_start = string8_first_sequence(string8_from_pointer_length(text.pointer + global_match, text.length - global_match), S8("(instrs "));
        if (instrs_start != BUSTER_STRING_NO_MATCH)
        {
            instrs_start = global_match + instrs_start + BUSTER_COMPILE_TIME_STRING_LENGTH("(instrs ");
            u64 instrs_end = instrs_start;
            while (instrs_end + 1 < text.length && !(text.pointer[instrs_end] == ')' && text.pointer[instrs_end + 1] == '>'))
            {
                instrs_end += 1;
            }

            String8 instruction_list = string8_from_pointer_length(text.pointer + instrs_start, instrs_end - instrs_start);
            u64 name_cursor = 0;
            while (name_cursor < instruction_list.length)
            {
                String8 tail = string8_from_pointer_length(instruction_list.pointer + name_cursor, instruction_list.length - name_cursor);
                String8 name = scrape_llvm_first_identifier(tail);
                if (name.length == 0)
                {
                    break;
                }

                scrape_llvm_add_instruction_schedule(database, model_index, name, schedule_write_name, read_advance_names, read_advance_count);
                name_cursor = (u64)(name.pointer - instruction_list.pointer) + name.length;
            }
        }

        cursor = global_match + 1;
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_schedrw_contexts(ScrapeLlvmDatabase* database, String8 text, u32 model_index)
{
    char inherited_writes[SCRAPE_LLVM_SCOPE_DEPTH][SCRAPE_LLVM_NAME_CAPACITY] = { 0 };
    u32 depth = 0;
    u64 cursor = 0;

    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 line_end = string8_first_sequence(remaining, S8("\n"));
        if (line_end == BUSTER_STRING_NO_MATCH)
        {
            line_end = remaining.length;
        }

        String8 line = string8_from_pointer_length(remaining.pointer, line_end);
        String8 trimmed = scrape_llvm_trim(line);

        if (string8_first_sequence(trimmed, S8("SchedRW = [")) != BUSTER_STRING_NO_MATCH &&
            string8_first_sequence(trimmed, S8("in {")) != BUSTER_STRING_NO_MATCH)
        {
            u64 bracket_start = string8_first_sequence(trimmed, S8("["));
            u64 bracket_end = string8_first_sequence(trimmed, S8("]"));
            if (bracket_start != BUSTER_STRING_NO_MATCH && bracket_end != BUSTER_STRING_NO_MATCH && bracket_end > bracket_start)
            {
                String8 write_list = string8_from_pointer_length(trimmed.pointer + bracket_start + 1, bracket_end - bracket_start - 1);
                String8 write_name = scrape_llvm_first_identifier(write_list);
                if (write_name.length > 0 && depth + 1 < SCRAPE_LLVM_SCOPE_DEPTH)
                {
                    scrape_llvm_copy_name(inherited_writes[depth + 1], write_name);
                }
            }
        }

        if (string8_starts_with_sequence(trimmed, S8("def ")))
        {
            String8 instruction_name = scrape_llvm_identifier_after_keyword(trimmed, S8("def "));
            String8 explicit_sched = { 0 };
            u64 sched_start = string8_first_sequence(trimmed, S8("Sched<["));
            if (sched_start != BUSTER_STRING_NO_MATCH)
            {
                String8 sched_text = string8_from_pointer_length(trimmed.pointer + sched_start + BUSTER_COMPILE_TIME_STRING_LENGTH("Sched<["), trimmed.length - sched_start - BUSTER_COMPILE_TIME_STRING_LENGTH("Sched<["));
                explicit_sched = scrape_llvm_first_identifier(sched_text);
            }

            if (explicit_sched.length > 0)
            {
                scrape_llvm_add_instruction_schedule(database, model_index, instruction_name, explicit_sched, 0, 0);
            }
            else if (depth < SCRAPE_LLVM_SCOPE_DEPTH && inherited_writes[depth][0] != 0)
            {
                scrape_llvm_add_instruction_schedule(database, model_index, instruction_name, scrape_llvm_name_string8(inherited_writes[depth]), 0, 0);
            }
        }

        u32 open_count = 0;
        u32 close_count = 0;
        scrape_llvm_count_braces_outside_quotes(line, &open_count, &close_count);

        for (u32 open_i = 0; open_i < open_count; open_i += 1)
        {
            if (depth + 1 < SCRAPE_LLVM_SCOPE_DEPTH)
            {
                depth += 1;
                memcpy(inherited_writes[depth], inherited_writes[depth - 1], SCRAPE_LLVM_NAME_CAPACITY);
            }
        }

        for (u32 close_i = 0; close_i < close_count; close_i += 1)
        {
            if (depth > 0)
            {
                memset(inherited_writes[depth], 0, SCRAPE_LLVM_NAME_CAPACITY);
                depth -= 1;
            }
        }

        cursor += line_end;
        if (cursor < text.length && text.pointer[cursor] == '\n')
        {
            cursor += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL void scrape_llvm_parse_file(ScrapeLlvmDatabase* database, String8 text)
{
    scrape_llvm_parse_model_defs(database, text);

    u64 cursor = 0;
    while (cursor < text.length)
    {
        String8 remaining = string8_from_pointer_length(text.pointer + cursor, text.length - cursor);
        u64 line_end = string8_first_sequence(remaining, S8("\n"));
        if (line_end == BUSTER_STRING_NO_MATCH)
        {
            line_end = remaining.length;
        }

        scrape_llvm_parse_definition_line(database, string8_from_pointer_length(remaining.pointer, line_end));
        cursor += line_end;
        if (cursor < text.length && text.pointer[cursor] == '\n')
        {
            cursor += 1;
        }
    }

    String8 model_name = scrape_llvm_first_model_name_in_file(text);
    u32 model_index = scrape_llvm_find_processor_model_index(database, model_name);
    if (model_index != 0xffffffffu)
    {
        scrape_llvm_parse_processor_resources(database, text, model_index);
        scrape_llvm_parse_read_advances(database, text, model_index);
        scrape_llvm_parse_schedwrite_defs(database, text, model_index);
        scrape_llvm_parse_instrw_entries(database, text, model_index);
        scrape_llvm_parse_schedrw_contexts(database, text, model_index);
    }
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_string(OsFileDescriptor* file, String8 text)
{
    bool result = file != 0;
    if (result && text.length > 0)
    {
        os_file_write(file, BUSTER_SLICE_TO_BYTE_SLICE(text));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_format(OsFileDescriptor* file, String8 format, ...)
{
    char8 buffer[4096];
    String8 buffer_slice = { .pointer = buffer, .length = sizeof(buffer) - 1 };
    va_list variable_arguments;
    va_start(variable_arguments, format);
    StringFormatResult format_result = string8_format_va(buffer_slice, format, variable_arguments);
    va_end(variable_arguments);

    bool result = format_result.real_buffer_index == format_result.needed_code_unit_count;
    if (result)
    {
        String8 text = string8_from_pointer_length(buffer, format_result.real_buffer_index);
        result = scrape_llvm_write_string(file, text);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_c_string_literal(OsFileDescriptor* file, String8 text)
{
    bool result = scrape_llvm_write_string(file, S8("\""));
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        u8 character = text.pointer[i];
        if (character == '\\' || character == '"')
        {
            char8 escaped[] = { '\\', (char8)character };
            result = scrape_llvm_write_string(file, string8_from_pointer_length(escaped, 2));
        }
        else if (character == '\n')
        {
            result = scrape_llvm_write_string(file, S8("\\n"));
        }
        else if (character == '\r')
        {
            result = scrape_llvm_write_string(file, S8("\\r"));
        }
        else if (character == '\t')
        {
            result = scrape_llvm_write_string(file, S8("\\t"));
        }
        else
        {
            char8 plain[] = { (char8)character };
            result = scrape_llvm_write_string(file, string8_from_pointer_length(plain, 1));
        }
    }
    if (result)
    {
        result = scrape_llvm_write_string(file, S8("\""));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_u32(OsFileDescriptor* file, u32 value)
{
    char buffer[32];
    int count = snprintf(buffer, sizeof(buffer), "%u", value);
    bool result = count > 0 && (u32)count < sizeof(buffer);
    if (result)
    {
        result = scrape_llvm_write_string(file, string8_from_pointer_length((char8*)buffer, (u64)count));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_s32(OsFileDescriptor* file, s32 value)
{
    char buffer[32];
    int count = snprintf(buffer, sizeof(buffer), "%d", value);
    bool result = count > 0 && (u32)count < sizeof(buffer);
    if (result)
    {
        result = scrape_llvm_write_string(file, string8_from_pointer_length((char8*)buffer, (u64)count));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_identifier_suffix(OsFileDescriptor* file, String8 text)
{
    bool result = true;
    if (text.length == 0 || (text.pointer[0] >= '0' && text.pointer[0] <= '9'))
    {
        result = scrape_llvm_write_string(file, S8("N"));
    }
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        u8 character = text.pointer[i];
        char8 output = '_';
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9'))
        {
            output = (char8)character;
        }
        result = scrape_llvm_write_string(file, string8_from_pointer_length(&output, 1));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_instruction_id(OsFileDescriptor* file, u32 index, String8 name)
{
    BUSTER_UNUSED(index);
    bool result = scrape_llvm_write_string(file, S8("X86_SELECTOR_LLVM_INSTRUCTION_"));
    result = result && scrape_llvm_write_identifier_suffix(file, name);
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_processor_model_duplicate_count_before(ScrapeLlvmDatabase* database, u32 model_index)
{
    u32 result = 0;
    String8 target_name = scrape_llvm_name_string8(database->processor_models[model_index].name);
    for (u32 i = 0; i < model_index; i += 1)
    {
        if (string_equal(scrape_llvm_name_string8(database->processor_models[i].name), target_name))
        {
            result += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_processor_model_id(OsFileDescriptor* file, ScrapeLlvmDatabase* database, u32 model_index)
{
    bool result = scrape_llvm_write_string(file, S8("X86_SELECTOR_LLVM_PROCESSOR_MODEL_"));
    result = result && scrape_llvm_write_identifier_suffix(file, scrape_llvm_name_string8(database->processor_models[model_index].name));
    u32 duplicate_index = scrape_llvm_processor_model_duplicate_count_before(database, model_index);
    if (duplicate_index != 0)
    {
        result = result && scrape_llvm_write_string(file, S8("_"));
        result = result && scrape_llvm_write_u32(file, duplicate_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_processor_resource_duplicate_count_before(ScrapeLlvmDatabase* database, u32 resource_index)
{
    u32 result = 0;
    String8 target_name = scrape_llvm_name_string8(database->processor_resources[resource_index].name);
    for (u32 i = 0; i < resource_index; i += 1)
    {
        if (string_equal(scrape_llvm_name_string8(database->processor_resources[i].name), target_name))
        {
            result += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_processor_resource_id(OsFileDescriptor* file, ScrapeLlvmDatabase* database, u32 resource_index)
{
    bool result = scrape_llvm_write_string(file, S8("X86_SELECTOR_LLVM_PROCESSOR_RESOURCE_"));
    result = result && scrape_llvm_write_identifier_suffix(file, scrape_llvm_name_string8(database->processor_resources[resource_index].name));
    u32 duplicate_index = scrape_llvm_processor_resource_duplicate_count_before(database, resource_index);
    if (duplicate_index != 0)
    {
        result = result && scrape_llvm_write_string(file, S8("_"));
        result = result && scrape_llvm_write_u32(file, duplicate_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_read_advance_duplicate_count_before(ScrapeLlvmDatabase* database, u32 read_advance_index)
{
    u32 result = 0;
    String8 target_name = scrape_llvm_name_string8(database->read_advances[read_advance_index].name);
    for (u32 i = 0; i < read_advance_index; i += 1)
    {
        if (string_equal(scrape_llvm_name_string8(database->read_advances[i].name), target_name))
        {
            result += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_read_advance_id(OsFileDescriptor* file, ScrapeLlvmDatabase* database, u32 read_advance_index)
{
    bool result = scrape_llvm_write_string(file, S8("X86_SELECTOR_LLVM_READ_ADVANCE_"));
    result = result && scrape_llvm_write_identifier_suffix(file, scrape_llvm_name_string8(database->read_advances[read_advance_index].name));
    u32 duplicate_index = scrape_llvm_read_advance_duplicate_count_before(database, read_advance_index);
    if (duplicate_index != 0)
    {
        result = result && scrape_llvm_write_string(file, S8("_"));
        result = result && scrape_llvm_write_u32(file, duplicate_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 scrape_llvm_schedule_write_duplicate_count_before(ScrapeLlvmDatabase* database, u32 write_index)
{
    u32 result = 0;
    if (write_index < database->schedule_write_count)
    {
        String8 target_name = scrape_llvm_name_string8(database->schedule_writes[write_index].name);
        for (u32 i = 0; i < write_index; i += 1)
        {
            if (string_equal(scrape_llvm_name_string8(database->schedule_writes[i].name), target_name))
            {
                result += 1;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_schedule_write_id(OsFileDescriptor* file, ScrapeLlvmDatabase* database, u32 index, String8 name)
{
    bool result = scrape_llvm_write_string(file, S8("X86_SELECTOR_LLVM_SCHEDULE_WRITE_"));
    result = result && scrape_llvm_write_identifier_suffix(file, name);
    u32 duplicate_index = scrape_llvm_schedule_write_duplicate_count_before(database, index);
    if (duplicate_index != 0)
    {
        result = result && scrape_llvm_write_string(file, S8("_"));
        result = result && scrape_llvm_write_u32(file, duplicate_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_llvm_write_generated_source(StringOs output_path, ScrapeLlvmDatabase* database)
{
    u32 scheduled_instruction_indices[SCRAPE_LLVM_MAX_INSTRUCTION_SCHEDULE_COUNT] = { 0 };
    u32 scheduled_instruction_count = scrape_llvm_collect_unique_scheduled_instruction_indices(
        database,
        scheduled_instruction_indices,
        BUSTER_ARRAY_LENGTH(scheduled_instruction_indices));
    u32 preferred_model_index = scrape_llvm_find_processor_model_index(database, scrape_llvm_preferred_model_name());
    u32 processor_resource_member_count = 0;
    u32 schedule_write_resource_usage_count = 0;
    u32 instruction_schedule_read_advance_index_count = 0;

    for (u32 resource_i = 0; resource_i < database->processor_resource_count; resource_i += 1)
    {
        processor_resource_member_count += database->processor_resources[resource_i].member_count;
    }
    for (u32 write_i = 0; write_i < database->schedule_write_count; write_i += 1)
    {
        schedule_write_resource_usage_count += database->schedule_writes[write_i].resource_name_count;
    }
    for (u32 schedule_i = 0; schedule_i < database->instruction_schedule_count; schedule_i += 1)
    {
        instruction_schedule_read_advance_index_count += database->instruction_schedules[schedule_i].read_advance_count;
    }

    OsFileDescriptor* file = os_file_open(output_path, (OpenFlags){ .write = 1, .create = 1, .truncate = 1 }, (OpenPermissions){ .read = 1, .write = 1 });
    bool result = file != 0;

    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("// Auto-generated x86-64 LLVM TableGen data.\n"));
        result = result && scrape_llvm_write_string(file, S8("// Do not edit manually.\n\n"));
        result = result && scrape_llvm_write_string(file, S8("#pragma once\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmCost)\n{\n"));
        result = result && scrape_llvm_write_string(file, S8("    u32 latency;\n"));
        result = result && scrape_llvm_write_string(file, S8("    u32 micro_op_count;\n"));
        result = result && scrape_llvm_write_string(file, S8("    bool has_metrics;\n"));
        result = result && scrape_llvm_write_string(file, S8("    u8 reserved[3];\n"));
        result = result && scrape_llvm_write_string(file, S8("};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmProcessorModel)\n{\n"));
        result = result && scrape_llvm_write_string(file, S8("    u32 issue_width;\n    u32 micro_op_buffer_size;\n    u32 loop_micro_op_buffer_size;\n    u32 load_latency;\n    u32 vector_load_latency;\n    u32 store_latency;\n    u32 high_latency;\n    u32 mispredict_penalty;\n    bool complete_model;\n    u8 reserved[3];\n};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmProcessorResource)\n{\n"));
        result = result && scrape_llvm_write_string(file, S8("    u32 processor_model_id;\n    u32 units;\n    u32 buffer_size;\n    u32 member_index_start;\n    u16 member_count;\n    bool is_group;\n    u8 reserved;\n};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmProcessorResourceMember)\n{\n    u32 processor_resource_id;\n};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmReadAdvance)\n{\n    u32 processor_model_id;\n    s32 cycles;\n};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmWriteResourceUsage)\n{\n    u32 processor_resource_id;\n    u32 release_at_cycle;\n};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmScheduleWrite)\n{\n"));
        result = result && scrape_llvm_write_string(file, S8("    u32 processor_model_id;\n    u32 primary_write_index;\n    u32 resource_usage_start;\n    u16 resource_usage_count;\n    bool is_variant;\n    u8 reserved;\n    X86SelectorLlvmCost cost;\n};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("STRUCT(X86SelectorLlvmInstructionSchedule)\n{\n"));
        result = result && scrape_llvm_write_string(file, S8("    u32 processor_model_id;\n    u32 instruction_id;\n    u32 schedule_write_id;\n    u32 read_advance_index_start;\n    u16 read_advance_index_count;\n    u16 reserved;\n};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("typedef X86SelectorLlvmCost X86SelectorLlvmLoweringCost;\n\n"));
    }

    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("ENUM(X86SelectorLlvmProcessorModelId,\n"));
    }
    for (u32 model_i = 0; result && model_i < database->processor_model_count; model_i += 1)
    {
        result = result && scrape_llvm_write_string(file, S8("    "));
        result = result && scrape_llvm_write_processor_model_id(file, database, model_i);
        result = result && scrape_llvm_write_format(file, S8(" = {u32},\n"), model_i);
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("    X86_SELECTOR_LLVM_PROCESSOR_MODEL_COUNT,\n);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL String8 x86_selector_llvm_processor_model_names[] = {\n"));
    }
    for (u32 model_i = 0; result && model_i < database->processor_model_count; model_i += 1)
    {
        String8 name = scrape_llvm_name_string8(database->processor_models[model_i].name);
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_processor_model_id(file, database, model_i);
        result = result && scrape_llvm_write_string(file, S8("] = S8("));
        result = result && scrape_llvm_write_c_string_literal(file, name);
        result = result && scrape_llvm_write_string(file, S8("),\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_processor_model_names) == X86_SELECTOR_LLVM_PROCESSOR_MODEL_COUNT);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmProcessorModel x86_selector_llvm_processor_models[] = {\n"));
    }
    for (u32 model_i = 0; result && model_i < database->processor_model_count; model_i += 1)
    {
        ScrapeLlvmProcessorModel* model = &database->processor_models[model_i];
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_processor_model_id(file, database, model_i);
        result = result && scrape_llvm_write_string(file, S8("] = {"));
        if (model->issue_width) result = result && scrape_llvm_write_format(file, S8(" .issue_width = {u32},"), model->issue_width);
        if (model->micro_op_buffer_size) result = result && scrape_llvm_write_format(file, S8(" .micro_op_buffer_size = {u32},"), model->micro_op_buffer_size);
        if (model->loop_micro_op_buffer_size) result = result && scrape_llvm_write_format(file, S8(" .loop_micro_op_buffer_size = {u32},"), model->loop_micro_op_buffer_size);
        if (model->load_latency) result = result && scrape_llvm_write_format(file, S8(" .load_latency = {u32},"), model->load_latency);
        if (model->vector_load_latency) result = result && scrape_llvm_write_format(file, S8(" .vector_load_latency = {u32},"), model->vector_load_latency);
        if (model->store_latency) result = result && scrape_llvm_write_format(file, S8(" .store_latency = {u32},"), model->store_latency);
        if (model->high_latency) result = result && scrape_llvm_write_format(file, S8(" .high_latency = {u32},"), model->high_latency);
        if (model->mispredict_penalty) result = result && scrape_llvm_write_format(file, S8(" .mispredict_penalty = {u32},"), model->mispredict_penalty);
        if (model->complete_model) result = result && scrape_llvm_write_string(file, S8(" .complete_model = true,"));
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_processor_models) == X86_SELECTOR_LLVM_PROCESSOR_MODEL_COUNT);\n\n"));
    }

    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("ENUM(X86SelectorLlvmProcessorResourceId,\n"));
    }
    for (u32 resource_i = 0; result && resource_i < database->processor_resource_count; resource_i += 1)
    {
        result = result && scrape_llvm_write_string(file, S8("    "));
        result = result && scrape_llvm_write_processor_resource_id(file, database, resource_i);
        result = result && scrape_llvm_write_format(file, S8(" = {u32},\n"), resource_i);
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("    X86_SELECTOR_LLVM_PROCESSOR_RESOURCE_COUNT,\n);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL String8 x86_selector_llvm_processor_resource_names[] = {\n"));
    }
    for (u32 resource_i = 0; result && resource_i < database->processor_resource_count; resource_i += 1)
    {
        String8 name = scrape_llvm_name_string8(database->processor_resources[resource_i].name);
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_processor_resource_id(file, database, resource_i);
        result = result && scrape_llvm_write_string(file, S8("] = S8("));
        result = result && scrape_llvm_write_c_string_literal(file, name);
        result = result && scrape_llvm_write_string(file, S8("),\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_processor_resource_names) == X86_SELECTOR_LLVM_PROCESSOR_RESOURCE_COUNT);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmProcessorResourceMember x86_selector_llvm_processor_resource_members[] = {\n"));
    }
    u32 member_cursor = 0;
    for (u32 resource_i = 0; result && resource_i < database->processor_resource_count; resource_i += 1)
    {
        ScrapeLlvmProcessorResource* resource = &database->processor_resources[resource_i];
        for (u32 member_i = 0; result && member_i < resource->member_count; member_i += 1)
        {
            u32 member_resource_index = scrape_llvm_find_processor_resource_index(database, resource->model_index, scrape_llvm_name_string8(resource->member_names[member_i]));
            result = result && scrape_llvm_write_string(file, S8("    ["));
            result = result && scrape_llvm_write_u32(file, member_cursor);
            result = result && scrape_llvm_write_string(file, S8("] = { .processor_resource_id = "));
            if (member_resource_index != 0xffffffffu)
            {
                result = result && scrape_llvm_write_processor_resource_id(file, database, member_resource_index);
            }
            else
            {
                result = result && scrape_llvm_write_string(file, S8("0"));
            }
            result = result && scrape_llvm_write_string(file, S8(" },\n"));
            member_cursor += 1;
        }
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmProcessorResource x86_selector_llvm_processor_resources[] = {\n"));
    }
    member_cursor = 0;
    for (u32 resource_i = 0; result && resource_i < database->processor_resource_count; resource_i += 1)
    {
        ScrapeLlvmProcessorResource* resource = &database->processor_resources[resource_i];
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_processor_resource_id(file, database, resource_i);
        result = result && scrape_llvm_write_string(file, S8("] = { .processor_model_id = "));
        result = result && scrape_llvm_write_processor_model_id(file, database, resource->model_index);
        if (resource->units) result = result && scrape_llvm_write_format(file, S8(", .units = {u32}"), resource->units);
        if (resource->buffer_size) result = result && scrape_llvm_write_format(file, S8(", .buffer_size = {u32}"), resource->buffer_size);
        if (resource->member_count) result = result && scrape_llvm_write_format(file, S8(", .member_index_start = {u32}, .member_count = {u32}"), member_cursor, resource->member_count);
        if (resource->is_group) result = result && scrape_llvm_write_string(file, S8(", .is_group = true"));
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
        member_cursor += resource->member_count;
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_processor_resources) == X86_SELECTOR_LLVM_PROCESSOR_RESOURCE_COUNT);\n\n"));
    }

    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("ENUM(X86SelectorLlvmReadAdvanceId,\n"));
    }
    for (u32 read_advance_i = 0; result && read_advance_i < database->read_advance_count; read_advance_i += 1)
    {
        result = result && scrape_llvm_write_string(file, S8("    "));
        result = result && scrape_llvm_write_read_advance_id(file, database, read_advance_i);
        result = result && scrape_llvm_write_format(file, S8(" = {u32},\n"), read_advance_i);
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("    X86_SELECTOR_LLVM_READ_ADVANCE_COUNT,\n);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL String8 x86_selector_llvm_read_advance_names[] = {\n"));
    }
    for (u32 read_advance_i = 0; result && read_advance_i < database->read_advance_count; read_advance_i += 1)
    {
        String8 name = scrape_llvm_name_string8(database->read_advances[read_advance_i].name);
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_read_advance_id(file, database, read_advance_i);
        result = result && scrape_llvm_write_string(file, S8("] = S8("));
        result = result && scrape_llvm_write_c_string_literal(file, name);
        result = result && scrape_llvm_write_string(file, S8("),\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_read_advance_names) == X86_SELECTOR_LLVM_READ_ADVANCE_COUNT);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmReadAdvance x86_selector_llvm_read_advances[] = {\n"));
    }
    for (u32 read_advance_i = 0; result && read_advance_i < database->read_advance_count; read_advance_i += 1)
    {
        ScrapeLlvmReadAdvance* read_advance = &database->read_advances[read_advance_i];
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_read_advance_id(file, database, read_advance_i);
        result = result && scrape_llvm_write_string(file, S8("] = { .processor_model_id = "));
        result = result && scrape_llvm_write_processor_model_id(file, database, read_advance->model_index);
        result = result && scrape_llvm_write_string(file, S8(", .cycles = "));
        result = result && scrape_llvm_write_s32(file, read_advance->cycles);
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_read_advances) == X86_SELECTOR_LLVM_READ_ADVANCE_COUNT);\n\n"));
    }

    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("ENUM(X86SelectorLlvmInstructionId,\n"));
    }
    for (u32 instruction_i = 0; result && instruction_i < scheduled_instruction_count; instruction_i += 1)
    {
        ScrapeLlvmInstructionSchedule* instruction_schedule = &database->instruction_schedules[scheduled_instruction_indices[instruction_i]];
        result = result && scrape_llvm_write_string(file, S8("    "));
        result = result && scrape_llvm_write_instruction_id(file, instruction_i, scrape_llvm_name_string8(instruction_schedule->instruction_name));
        result = result && scrape_llvm_write_format(file, S8(" = {u32},\n"), instruction_i);
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("    X86_SELECTOR_LLVM_INSTRUCTION_COUNT,\n);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL String8 x86_selector_llvm_instruction_names[] = {\n"));
    }
    for (u32 instruction_i = 0; result && instruction_i < scheduled_instruction_count; instruction_i += 1)
    {
        ScrapeLlvmInstructionSchedule* instruction_schedule = &database->instruction_schedules[scheduled_instruction_indices[instruction_i]];
        String8 instruction_name = scrape_llvm_name_string8(instruction_schedule->instruction_name);
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_instruction_id(file, instruction_i, instruction_name);
        result = result && scrape_llvm_write_string(file, S8("] = S8("));
        result = result && scrape_llvm_write_c_string_literal(file, instruction_name);
        result = result && scrape_llvm_write_string(file, S8("),\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_instruction_names) == X86_SELECTOR_LLVM_INSTRUCTION_COUNT);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmCost x86_selector_llvm_instruction_costs[] = {\n"));
    }
    for (u32 instruction_i = 0; result && instruction_i < scheduled_instruction_count; instruction_i += 1)
    {
        ScrapeLlvmInstructionSchedule* instruction_schedule = &database->instruction_schedules[scheduled_instruction_indices[instruction_i]];
        String8 instruction_name = scrape_llvm_name_string8(instruction_schedule->instruction_name);
        ScrapeLlvmResolvedCost cost = { 0 };
        if (preferred_model_index != 0xffffffffu)
        {
            cost = scrape_llvm_find_instruction_cost_for_model(database, preferred_model_index, instruction_name);
        }
        if (!cost.has_metrics)
        {
            for (u32 model_i = 0; !cost.has_metrics && model_i < database->processor_model_count; model_i += 1)
            {
                cost = scrape_llvm_find_instruction_cost_for_model(database, model_i, instruction_name);
            }
        }
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_instruction_id(file, instruction_i, instruction_name);
        result = result && scrape_llvm_write_string(file, S8("] = {"));
        if (cost.latency) result = result && scrape_llvm_write_format(file, S8(" .latency = {u32},"), cost.latency);
        if (cost.micro_op_count) result = result && scrape_llvm_write_format(file, S8(" .micro_op_count = {u32},"), cost.micro_op_count);
        if (cost.has_metrics) result = result && scrape_llvm_write_string(file, S8(" .has_metrics = true,"));
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_instruction_costs) == X86_SELECTOR_LLVM_INSTRUCTION_COUNT);\n\n"));
    }

    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("ENUM(X86SelectorLlvmScheduleWriteId,\n"));
    }
    for (u32 write_i = 0; result && write_i < database->schedule_write_count; write_i += 1)
    {
        ScrapeLlvmScheduleWrite* write = &database->schedule_writes[write_i];
        result = result && scrape_llvm_write_string(file, S8("    "));
        result = result && scrape_llvm_write_schedule_write_id(file, database, write_i, scrape_llvm_name_string8(write->name));
        result = result && scrape_llvm_write_format(file, S8(" = {u32},\n"), write_i);
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("    X86_SELECTOR_LLVM_SCHEDULE_WRITE_COUNT,\n);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL String8 x86_selector_llvm_schedule_write_names[] = {\n"));
    }
    for (u32 write_i = 0; result && write_i < database->schedule_write_count; write_i += 1)
    {
        ScrapeLlvmScheduleWrite* write = &database->schedule_writes[write_i];
        String8 write_name = scrape_llvm_name_string8(write->name);
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_schedule_write_id(file, database, write_i, write_name);
        result = result && scrape_llvm_write_string(file, S8("] = S8("));
        result = result && scrape_llvm_write_c_string_literal(file, write_name);
        result = result && scrape_llvm_write_string(file, S8("),\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_schedule_write_names) == X86_SELECTOR_LLVM_SCHEDULE_WRITE_COUNT);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmCost x86_selector_llvm_schedule_write_costs[] = {\n"));
    }
    for (u32 write_i = 0; result && write_i < database->schedule_write_count; write_i += 1)
    {
        ScrapeLlvmScheduleWrite* write = &database->schedule_writes[write_i];
        ScrapeLlvmResolvedCost cost = scrape_llvm_resolve_schedule_write_index(database, write_i);
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_schedule_write_id(file, database, write_i, scrape_llvm_name_string8(write->name));
        result = result && scrape_llvm_write_string(file, S8("] = {"));
        if (cost.latency) result = result && scrape_llvm_write_format(file, S8(" .latency = {u32},"), cost.latency);
        if (cost.micro_op_count) result = result && scrape_llvm_write_format(file, S8(" .micro_op_count = {u32},"), cost.micro_op_count);
        if (cost.has_metrics) result = result && scrape_llvm_write_string(file, S8(" .has_metrics = true,"));
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_schedule_write_costs) == X86_SELECTOR_LLVM_SCHEDULE_WRITE_COUNT);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmWriteResourceUsage x86_selector_llvm_write_resource_usages[] = {\n"));
    }
    u32 write_usage_cursor = 0;
    for (u32 write_i = 0; result && write_i < database->schedule_write_count; write_i += 1)
    {
        ScrapeLlvmScheduleWrite* write = &database->schedule_writes[write_i];
        for (u32 resource_i = 0; result && resource_i < write->resource_name_count; resource_i += 1)
        {
            u32 processor_resource_index = scrape_llvm_find_processor_resource_index(database, write->model_index, scrape_llvm_name_string8(write->resource_names[resource_i]));
            result = result && scrape_llvm_write_string(file, S8("    ["));
            result = result && scrape_llvm_write_u32(file, write_usage_cursor);
            result = result && scrape_llvm_write_string(file, S8("] = { .processor_resource_id = "));
            if (processor_resource_index != 0xffffffffu)
            {
                result = result && scrape_llvm_write_processor_resource_id(file, database, processor_resource_index);
            }
            else
            {
                result = result && scrape_llvm_write_string(file, S8("0"));
            }
            if (write->release_cycles[resource_i] != 0) result = result && scrape_llvm_write_format(file, S8(", .release_at_cycle = {u32}"), write->release_cycles[resource_i]);
            result = result && scrape_llvm_write_string(file, S8(" },\n"));
            write_usage_cursor += 1;
        }
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmScheduleWrite x86_selector_llvm_schedule_writes[] = {\n"));
    }
    write_usage_cursor = 0;
    for (u32 write_i = 0; result && write_i < database->schedule_write_count; write_i += 1)
    {
        ScrapeLlvmScheduleWrite* write = &database->schedule_writes[write_i];
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_schedule_write_id(file, database, write_i, scrape_llvm_name_string8(write->name));
        result = result && scrape_llvm_write_string(file, S8("] = { .processor_model_id = "));
        result = result && scrape_llvm_write_processor_model_id(file, database, write->model_index);
        if (write->primary_write_name[0] != 0)
        {
            u32 primary_write_index = scrape_llvm_find_schedule_write_index_for_model(database, write->model_index, scrape_llvm_name_string8(write->primary_write_name));
            if (primary_write_index != 0xffffffffu)
            {
                result = result && scrape_llvm_write_string(file, S8(", .primary_write_index = "));
                result = result && scrape_llvm_write_schedule_write_id(file, database, primary_write_index, scrape_llvm_name_string8(database->schedule_writes[primary_write_index].name));
            }
        }
        if (write->resource_name_count != 0) result = result && scrape_llvm_write_format(file, S8(", .resource_usage_start = {u32}, .resource_usage_count = {u32}"), write_usage_cursor, write->resource_name_count);
        if (write->is_variant) result = result && scrape_llvm_write_string(file, S8(", .is_variant = true"));
        if (write->latency || write->micro_op_count || write->has_metrics)
        {
            result = result && scrape_llvm_write_string(file, S8(", .cost = {"));
            if (write->latency) result = result && scrape_llvm_write_format(file, S8(" .latency = {u32},"), write->latency);
            if (write->micro_op_count) result = result && scrape_llvm_write_format(file, S8(" .micro_op_count = {u32},"), write->micro_op_count);
            if (write->has_metrics) result = result && scrape_llvm_write_string(file, S8(" .has_metrics = true,"));
            result = result && scrape_llvm_write_string(file, S8(" }"));
        }
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
        write_usage_cursor += write->resource_name_count;
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_schedule_writes) == X86_SELECTOR_LLVM_SCHEDULE_WRITE_COUNT);\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL u32 x86_selector_llvm_instruction_schedule_read_advance_indices[] = {\n"));
    }
    u32 read_advance_cursor = 0;
    for (u32 schedule_i = 0; result && schedule_i < database->instruction_schedule_count; schedule_i += 1)
    {
        ScrapeLlvmInstructionSchedule* schedule = &database->instruction_schedules[schedule_i];
        for (u32 read_advance_i = 0; result && read_advance_i < schedule->read_advance_count; read_advance_i += 1)
        {
            u32 resolved_read_advance_index = scrape_llvm_find_read_advance_index(database, schedule->model_index, scrape_llvm_name_string8(schedule->read_advance_names[read_advance_i]));
            result = result && scrape_llvm_write_string(file, S8("    ["));
            result = result && scrape_llvm_write_u32(file, read_advance_cursor);
            result = result && scrape_llvm_write_string(file, S8("] = "));
            if (resolved_read_advance_index != 0xffffffffu)
            {
                result = result && scrape_llvm_write_read_advance_id(file, database, resolved_read_advance_index);
            }
            else
            {
                result = result && scrape_llvm_write_string(file, S8("0"));
            }
            result = result && scrape_llvm_write_string(file, S8(",\n"));
            read_advance_cursor += 1;
        }
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n\n"));
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmInstructionSchedule x86_selector_llvm_instruction_schedules[] = {\n"));
    }
    read_advance_cursor = 0;
    for (u32 schedule_i = 0; result && schedule_i < database->instruction_schedule_count; schedule_i += 1)
    {
        ScrapeLlvmInstructionSchedule* schedule = &database->instruction_schedules[schedule_i];
        u32 instruction_id = 0xffffffffu;
        for (u32 instruction_i = 0; instruction_i < scheduled_instruction_count; instruction_i += 1)
        {
            ScrapeLlvmInstructionSchedule* representative = &database->instruction_schedules[scheduled_instruction_indices[instruction_i]];
            if (scrape_llvm_name_equals(representative->instruction_name, scrape_llvm_name_string8(schedule->instruction_name)))
            {
                instruction_id = instruction_i;
                break;
            }
        }
        u32 schedule_write_id = scrape_llvm_find_schedule_write_index_for_model(database, schedule->model_index, scrape_llvm_name_string8(schedule->schedule_write_name));
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_u32(file, schedule_i);
        result = result && scrape_llvm_write_string(file, S8("] = { .processor_model_id = "));
        result = result && scrape_llvm_write_processor_model_id(file, database, schedule->model_index);
        if (instruction_id != 0xffffffffu)
        {
            result = result && scrape_llvm_write_string(file, S8(", .instruction_id = "));
            result = result && scrape_llvm_write_instruction_id(file, instruction_id, scrape_llvm_name_string8(schedule->instruction_name));
        }
        if (schedule_write_id != 0xffffffffu)
        {
            result = result && scrape_llvm_write_string(file, S8(", .schedule_write_id = "));
            result = result && scrape_llvm_write_schedule_write_id(file, database, schedule_write_id, scrape_llvm_name_string8(database->schedule_writes[schedule_write_id].name));
        }
        if (schedule->read_advance_count != 0) result = result && scrape_llvm_write_format(file, S8(", .read_advance_index_start = {u32}, .read_advance_index_count = {u32}"), read_advance_cursor, schedule->read_advance_count);
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
        read_advance_cursor += schedule->read_advance_count;
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n\n"));
    }

    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("BUSTER_GLOBAL_LOCAL X86SelectorLlvmLoweringCost x86_selector_llvm_lowering_costs[] = {\n"));
    }
    for (u32 lowering_i = 0; result && lowering_i < BUSTER_ARRAY_LENGTH(scrape_llvm_lowering_cost_specs); lowering_i += 1)
    {
        ScrapeLlvmLoweringCostSpec spec = scrape_llvm_lowering_cost_specs[lowering_i];
        ScrapeLlvmResolvedCost cost = scrape_llvm_resolve_lowering_cost(database, spec);
        result = result && scrape_llvm_write_string(file, S8("    ["));
        result = result && scrape_llvm_write_string(file, spec.lowering_name);
        result = result && scrape_llvm_write_string(file, S8("] = {"));
        if (cost.latency) result = result && scrape_llvm_write_format(file, S8(" .latency = {u32},"), cost.latency);
        if (cost.micro_op_count) result = result && scrape_llvm_write_format(file, S8(" .micro_op_count = {u32},"), cost.micro_op_count);
        if (cost.has_metrics) result = result && scrape_llvm_write_string(file, S8(" .has_metrics = true,"));
        result = result && scrape_llvm_write_string(file, S8(" },\n"));
    }
    if (result)
    {
        result = result && scrape_llvm_write_string(file, S8("};\n"));
        result = result && scrape_llvm_write_string(file, S8("static_assert(BUSTER_ARRAY_LENGTH(x86_selector_llvm_lowering_costs) == X86_SELECTOR_LOWERING_COUNT);\n"));
    }

    if (file)
    {
        os_file_close(file);
    }

    BUSTER_UNUSED(processor_resource_member_count);
    BUSTER_UNUSED(schedule_write_resource_usage_count);
    BUSTER_UNUSED(instruction_schedule_read_advance_index_count);
    return result;
}

BUSTER_IMPL ProcessResult process_arguments()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    let argv = program_state->input.argv;
    let envp = program_state->input.envp;
    let arg_it = string_os_list_iterator_initialize(argv);

    string_os_list_iterator_next(&arg_it);

    let first = string_os_list_iterator_next(&arg_it);
    if (!first.pointer)
    {
        string8_print(S8("Usage: scrape_llvm /path/to/llvm [--tblgen path] [--generate] [--generate-output path]\n"));
        return PROCESS_RESULT_FAILED;
    }

    if (string_equal(first, SOs("test")))
    {
        scrape_llvm_program_state.skip_generation = true;
        return PROCESS_RESULT_SUCCESS;
    }

    scrape_llvm_program_state.llvm_root = first;
    u64 i = 2;
    for (StringOs arg = string_os_list_iterator_next(&arg_it); arg.pointer; arg = string_os_list_iterator_next(&arg_it), i += 1)
    {
        if (string_equal(arg, SOs("--generate")))
        {
            scrape_llvm_program_state.generate = true;
        }
        else if (string_equal(arg, SOs("--tblgen")))
        {
            scrape_llvm_program_state.llvm_tblgen_path = string_os_list_iterator_next(&arg_it);
            i += 1;
        }
        else if (string_equal(arg, SOs("--generate-output")))
        {
            scrape_llvm_program_state.generate_output = string_os_list_iterator_next(&arg_it);
            i += 1;
        }
        else
        {
            let r = buster_argument_process(argv, envp, i, arg);
            if (r != PROCESS_RESULT_SUCCESS)
            {
                string8_print(S8("Unknown argument: {SOs}\n"), arg);
                result = r;
                break;
            }
        }
    }

    if (!scrape_llvm_program_state.generate)
    {
        scrape_llvm_program_state.generate = true;
    }

    return result;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    if (scrape_llvm_program_state.skip_generation || !scrape_llvm_program_state.llvm_root.pointer)
    {
        return PROCESS_RESULT_SUCCESS;
    }

    Arena* arena = thread_arena();
    ScrapeLlvmDatabase* database = arena_allocate(arena, ScrapeLlvmDatabase, 1);
    memset(database, 0, sizeof(*database));
    StringOs llvm_tblgen_path = scrape_llvm_program_state.llvm_tblgen_path.pointer ?
        scrape_llvm_program_state.llvm_tblgen_path :
        scrape_llvm_default_tblgen_path();

    bool parsed = scrape_llvm_build_database_from_tblgen_json(arena, database, scrape_llvm_program_state.llvm_root, llvm_tblgen_path);
    if (!parsed)
    {
        string8_print(S8("Failed to import LLVM TableGen data from {SOs} using {SOs}\n"), scrape_llvm_program_state.llvm_root, llvm_tblgen_path);
        return PROCESS_RESULT_FAILED;
    }

    StringOs output_path = scrape_llvm_program_state.generate_output.pointer ?
        scrape_llvm_program_state.generate_output :
        SOs("src/buster/x86_64_llvm.c");

    bool wrote = scrape_llvm_write_generated_source(output_path, database);
    if (!wrote)
    {
        string8_print(S8("Failed to write output: {SOs}\n"), output_path);
        return PROCESS_RESULT_FAILED;
    }

    string8_print(S8("Generated {SOs} with {u32} instructions, {u32} schedule writes, {u32} instruction schedules\n"),
                  output_path,
                  database->instruction_count,
                  database->schedule_write_count,
                  database->instruction_schedule_count);
    return PROCESS_RESULT_SUCCESS;
}
