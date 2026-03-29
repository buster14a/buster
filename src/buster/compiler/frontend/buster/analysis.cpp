#pragma once

#include <buster/compiler/frontend/buster/analysis.h>
#include <buster/file.h>
#include <buster/arena.h>
#include <buster/assertion.h>

#define XXH_IMPLEMENTATION
#define XXH_STATIC_LINKING_ONLY
#define XXH_INLINE_ALL
#include <xxhash/xxhash.h>

STRUCT(Slot)
{
    String8 name;
    u64 hash;
};

typedef u32 SlotIndex;

STRUCT(InternTable)
{
    Arena* data_arena;
    Arena* hash_table_arena;

    BUSTER_GLOBAL_LOCAL constexpr u64 divisor = sizeof(u64) + sizeof(u32);

    BUSTER_INLINE u64 get_hash_table_allocation_size()
    {
        let byte_size = hash_table_arena->position - arena_minimum_position;
        return byte_size;
    }

    BUSTER_INLINE u32 get_hash_table_capacity()
    {
        let byte_size = get_hash_table_allocation_size();
        BUSTER_CHECK(byte_size % divisor == 0);
        return (u32)(byte_size / divisor);
    }
};

BUSTER_GLOBAL_LOCAL constexpr u64 intern_table_start_element_count = 1 << 10;

BUSTER_GLOBAL_LOCAL void table_allocate(InternTable* table, u32 count)
{
    BUSTER_CHECK(table->hash_table_arena->position == arena_minimum_position);
    u64 allocation_size = (u64)count * InternTable::divisor;
    let allocation = arena_allocate_bytes(table->hash_table_arena, allocation_size, alignof(u64));
    memset(allocation, 0, allocation_size);
}
 
BUSTER_GLOBAL_LOCAL void grow(InternTable* table)
{
    let current_capacity = table->get_hash_table_capacity();
    BUSTER_CHECK(table->get_hash_table_allocation_size() % InternTable::divisor == 0);
    BUSTER_CHECK(current_capacity == table->get_hash_table_allocation_size() / 12);
    let target_capacity = current_capacity << 1;

    if (current_capacity)
    {
        let scratch = scratch_begin(0, 0);

        let allocation = arena_allocate_bytes(scratch.arena, table->get_hash_table_allocation_size(), alignof(u64));
        memcpy(allocation, (u8*)table->hash_table_arena + arena_minimum_position, table->hash_table_arena->position - arena_minimum_position);
        table_allocate(table, target_capacity);

        scratch_end(scratch);
    }
    else
    {
        table_allocate(table, target_capacity);
    }
    // TODO, this is wrong
    bool a = true;
    if (a) BUSTER_TRAP();
}

BUSTER_GLOBAL_LOCAL InternTable intern_table_create()
{
    InternTable result = {
        .data_arena = arena_create({}),
        .hash_table_arena = arena_create({}),
    };

    table_allocate(&result, intern_table_start_element_count);

    return result;
}

typedef u32 InternIndex;

BUSTER_GLOBAL_LOCAL u64 hash_string(String8 string)
{
    return XXH3_64bits(string.pointer, string.length);
}

BUSTER_GLOBAL_LOCAL InternIndex intern_string(InternTable* restrict table, String8 string)
{
    InternIndex result = {};
    BUSTER_UNUSED(table);
    hash_string(string);

    bool a= true;
    if(a)BUSTER_TRAP();

    return result;
}

STRUCT(Analyzer)
{
    InternTable intern_table;
};

BUSTER_GLOBAL_LOCAL Analyzer analyzer_initialize()
{
    Analyzer analyzer = {
        .intern_table = intern_table_create(),
    };
    return analyzer;
}

BUSTER_F_IMPL AnalysisResult analyze(ParserResult* restrict parsers, u64 parser_count)
{
    AnalysisResult result = {};
    Analyzer analyzer = analyzer_initialize();
            bool a = true;

    for (u64 parser_i = 0; parser_i < parser_count; parser_i += 1)
    {
        ParserResult& parser = parsers[parser_i];
        for (u32 tld_i = 0; tld_i < parser.top_level_declaration_count; tld_i += 1)
        {
            const auto& tld = parser.top_level_declarations[tld_i];
            let start_index = tld.parser_token_start;
            let end_index = tld.parser_token_end;
            for (u32 parser_token_index = start_index; parser_token_index < end_index; parser_token_index += 1)
            {
                let lexer_token_index = parser.parser_token_indices[parser_token_index];
                BUSTER_UNUSED(lexer_token_index);
                BUSTER_UNUSED(analyzer);

                if (a) BUSTER_TRAP();
            }
            if (a) BUSTER_TRAP();
        }
    }

    return result;
}

BUSTER_F_IMPL void analysis_experiments()
{
    Arena* arena = arena_create({});
    let source = BYTE_SLICE_TO_STRING(8, file_read(arena, SOs("tests/basic.bbb"), {}));
    let parser = parse(source.pointer, tokenize(arena, source.pointer, source.length));
    let a = hash_string(S8("FOOasdjaksdjkasd"));
    BUSTER_UNUSED(a);
    analyze(&parser, 1);
}
