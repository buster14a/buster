#pragma once

#include <buster/compiler/frontend/buster/analysis.h>
#include <buster/file.h>
#include <buster/arena.h>
#include <buster/assertion.h>
#include <buster/string.h>
#include <buster/compiler/intern_table.h>

STRUCT(Analyzer)
{
    InternTable intern_table;
    IrModule* module;
};

BUSTER_GLOBAL_LOCAL Analyzer* analyzer_initialize()
{
    let arena = arena_create({});
    let analyzer = arena_allocate(arena, Analyzer, 1);
    *analyzer = {
        .intern_table = intern_table_create(),
    };
    analyzer->module = ir_module_create(arena_create({}), 0, S8("module"), &analyzer->intern_table);
    return analyzer;
}

BUSTER_GLOBAL_LOCAL String8 function_attribute_names[] = {
    [(u64)IrFunctionAttribute::CallingConvention] = S8("cc"),
};

static_assert(BUSTER_ARRAY_LENGTH(function_attribute_names) == (u64)IrFunctionAttribute::Count);

BUSTER_GLOBAL_LOCAL String8 calling_convention_names[] = {
    [(u64)IrCallingConvention::C] = S8("c"),
    [(u64)IrCallingConvention::SystemV] = S8("systemv"),
    [(u64)IrCallingConvention::Win64] = S8("win64"),
};

static_assert(BUSTER_ARRAY_LENGTH(calling_convention_names) == (u64)IrCallingConvention::Count);

BUSTER_GLOBAL_LOCAL ParserTokenIndex token_not_found = { UINT32_MAX };

BUSTER_GLOBAL_LOCAL ParserTokenIndex next(const ParserResult& parser, TokenId id, ParserTokenIndex start, ParserTokenIndex end)
{
    ParserTokenIndex result = token_not_found;

    for (ParserTokenIndex parser_token_i = start; parser_token_i.v < end.v; parser_token_i.v += 1)
    {
        let lexer_token_i = parser.parser_indices[parser_token_i.v].v;
        let token = parser.lexer_tokens[lexer_token_i];
        if (token.id == id)
        {
            result = parser_token_i;
            break;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrFunctionAttributes analyze_function_attributes(const ParserResult& parser, ParserTokenIndex left_bracket_parser_index, ParserTokenIndex right_bracket_parser_index)
{
    IrFunctionAttributes attributes = {};
    Slice<String8> function_attribute_name_array = BUSTER_ARRAY_TO_SLICE(function_attribute_names);
    Slice<String8> calling_convention_name_array = BUSTER_ARRAY_TO_SLICE(calling_convention_names);

    for (ParserTokenIndex parser_token_i = { left_bracket_parser_index.v + 1 }; parser_token_i.v < right_bracket_parser_index.v; parser_token_i.v += 1)
    {
        let lexer_token_i = parser.parser_indices[parser_token_i.v];
        let token = &parser.lexer_tokens[lexer_token_i.v];

        if (token->id == TokenId::Identifier)
        {
            let attribute_identifier = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i.v);

            let function_attribute_i = string8_array_match(function_attribute_name_array, attribute_identifier);

            if (function_attribute_i != BUSTER_STRING_NO_MATCH)
            {
                let attribute = (IrFunctionAttribute)function_attribute_i;

                switch (attribute)
                {
                    break; case IrFunctionAttribute::CallingConvention:
                    {
                        ParserTokenIndex next_parser_token_i = { .v = parser_token_i.v  + 1 };

                        if (next_parser_token_i.v < right_bracket_parser_index.v)
                        {
                            lexer_token_i = parser.parser_indices[next_parser_token_i.v];
                            token = &parser.lexer_tokens[lexer_token_i.v];

                            if (token->id == TokenId::LeftParenthesis)
                            {
                                next_parser_token_i.v += 1;
                                if (next_parser_token_i.v >= right_bracket_parser_index.v)
                                {
                                    BUSTER_TRAP();
                                }

                                lexer_token_i = parser.parser_indices[next_parser_token_i.v];
                                token = &parser.lexer_tokens[lexer_token_i.v];
                                if (token->id != TokenId::Identifier)
                                {
                                    BUSTER_TRAP();
                                }

                                let calling_convention_name = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i.v);
                                let calling_convention_i = string8_array_match(calling_convention_name_array, calling_convention_name);
                                if (calling_convention_i == BUSTER_STRING_NO_MATCH)
                                {
                                    BUSTER_TRAP();
                                }

                                attributes.calling_convention = (IrCallingConvention)calling_convention_i;

                                next_parser_token_i.v += 1;
                                if (next_parser_token_i.v >= right_bracket_parser_index.v)
                                {
                                    BUSTER_TRAP();
                                }

                                lexer_token_i = parser.parser_indices[next_parser_token_i.v];
                                token = &parser.lexer_tokens[lexer_token_i.v];
                                if (token->id != TokenId::RightParenthesis)
                                {
                                    BUSTER_TRAP();
                                }

                                parser_token_i = next_parser_token_i;
                            }
                            else if (token->id == TokenId::Identifier)
                            {
                                let calling_convention_name = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i.v);
                                let calling_convention_i = string8_array_match(calling_convention_name_array, calling_convention_name);
                                if (calling_convention_i != BUSTER_STRING_NO_MATCH)
                                {
                                    attributes.calling_convention = (IrCallingConvention)calling_convention_i;
                                    parser_token_i = next_parser_token_i;
                                }
                                else
                                {
                                    BUSTER_TRAP();
                                }

                            }
                        }
                    }
                    break; default:
                    {
                        BUSTER_UNREACHABLE();
                    }
                }
            }
            else
            {
                BUSTER_TRAP();
            }
        }
        else
        {
            BUSTER_TRAP();
        }
    }

    return attributes;
}

BUSTER_GLOBAL_LOCAL String8 symbol_attribute_names[] = {
    [(u64)IrSymbolAttribute::Export] = S8("export"),
};

static_assert(BUSTER_ARRAY_LENGTH(symbol_attribute_names) == (u64)IrSymbolAttribute::Count);

BUSTER_GLOBAL_LOCAL IrSymbolAttributes analyze_symbol_attributes(const ParserResult& parser, ParserTokenIndex left_bracket_parser_index, ParserTokenIndex right_bracket_parser_index)
{
    IrSymbolAttributes attributes = {};
    Slice<String8> symbol_attribute_name_array = BUSTER_ARRAY_TO_SLICE(symbol_attribute_names);

    for (ParserTokenIndex parser_token_i = { .v = left_bracket_parser_index.v + 1 }; parser_token_i.v < right_bracket_parser_index.v; parser_token_i.v += 1)
    {
        let lexer_token_i = parser.parser_indices[parser_token_i.v];
        let token = &parser.lexer_tokens[lexer_token_i.v];

        if (token->id == TokenId::Identifier)
        {
            let attribute_identifier = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i.v);

            let symbol_attribute_i = string8_array_match(symbol_attribute_name_array, attribute_identifier);

            if (symbol_attribute_i != BUSTER_STRING_NO_MATCH)
            {
                let attribute = (IrSymbolAttribute)symbol_attribute_i;

                switch (attribute)
                {
                    break; case IrSymbolAttribute::Export:
                    {
                        attributes.exported = true;
                        attributes.linkage = IrLinkage::External;
                    }
                    break; case IrSymbolAttribute::Count: BUSTER_UNREACHABLE();
                }
            }
            else
            {
                BUSTER_TRAP();
            }
        }
        else
        {
            BUSTER_TRAP();
        }
    }

    return attributes;
}

BUSTER_GLOBAL_LOCAL void analyze_type(Analyzer* analyzer, const ParserResult& parser, ParserTokenIndex type_start, ParserTokenIndex type_end)
{
    BUSTER_UNUSED(analyzer);
    BUSTER_UNUSED(parser);
    BUSTER_UNUSED(type_start);
    BUSTER_UNUSED(type_end);

    let parser_token_i = type_start;
    let lexer_token_i = parser.parser_indices[parser_token_i.v];
    let token = parser.lexer_tokens[lexer_token_i.v];

    switch (token.id)
    {
        break; case TokenId::Ampersand:
        {
            // Element type
            analyze_type(analyzer, parser, { type_start.v + 1 }, type_end);
        }
        break; case TokenId::Identifier:
        {
            let identifier = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i.v);

            bool is_builtin = false;

            if (identifier.length >= 2)
            {
                bool is_decimal_after_first_digit = true;
                String8 decimal_slice = string8_slice(identifier, 1, identifier.length);

                for (u64 i = 1; i < identifier.length; i += 1)
                {
                    let ch = identifier.pointer[i];
                    if (!code_unit8_is_decimal(ch))
                    {
                        is_decimal_after_first_digit = false;
                        break;
                    }
                }

                if (is_decimal_after_first_digit)
                {
                    let bit_count = string8_parse_u64_decimal(decimal_slice.pointer).value;

                    if (identifier.pointer[0] == 'u' || identifier.pointer[0] == 's')
                    {
                        bool standard_int = is_power_of_two(bit_count);
                        if (standard_int)
                        {
                            is_builtin = true;
                        }
                        else
                        {
                            BUSTER_TRAP();
                        }
                    }
                    else if (identifier.pointer[0] == 'f')
                    {
                        bool standard_float = bit_count == 32 || bit_count == 64;
                        if (standard_float)
                        {
                            is_builtin = true;
                        }
                        else
                        {
                            BUSTER_TRAP();
                        }
                    }
                }
            }

            if (!is_builtin)
            {
                BUSTER_TRAP();
            }
        }
        break; default: BUSTER_UNREACHABLE();
    }
}

BUSTER_GLOBAL_LOCAL void analyze_argument_declarations(Analyzer* analyzer, const ParserResult& parser, ParserTokenIndex left_parenthesis_parser_index, ParserTokenIndex right_parenthesis_parser_index)
{
    ParserTokenIndex parser_token_i = { left_parenthesis_parser_index.v + 1 };

    while (parser_token_i.v < right_parenthesis_parser_index.v)
    {
        {
            u32 i = 0;
            ParserTokenIndex parser_i = { parser_token_i.v + i };
            let lexer_i = parser.parser_indices[parser_i.v];
            let token = parser.lexer_tokens[lexer_i.v];
            BUSTER_CHECK(token.id == TokenId::Colon);
        }

        {
            u32 i = 1;
            ParserTokenIndex parser_i = { parser_token_i.v + i };
            let lexer_i = parser.parser_indices[parser_i.v];
            let token = parser.lexer_tokens[lexer_i.v];
            BUSTER_CHECK(token.id == TokenId::Identifier);
        }

        let comma = next(parser, TokenId::Comma, parser_token_i, right_parenthesis_parser_index);
        let argument_end = comma.v == token_not_found.v ? right_parenthesis_parser_index : comma;

        analyze_type(analyzer, parser, { parser_token_i.v + 2 }, argument_end);

        parser_token_i = { argument_end.v + (argument_end.v == comma.v) };
    }
}

BUSTER_GLOBAL_LOCAL void analyze_function_header(Analyzer* analyzer, const ParserResult& parser, ParserTokenIndex parser_start_index, ParserTokenIndex parser_end_index)
{
    bool function_attributes_already_parsed = false;
    bool symbol_attributes_already_parsed = false;
    bool name_already_parsed = false;
    ParserTokenIndex name_token_index = { token_not_found.v - 1 };
    bool argument_parsed = false;

    IrFunctionAttributes function_attributes = {};
    IrSymbolAttributes symbol_attributes = {};

    ParserTokenIndex parser_token_i;
    for (parser_token_i = {parser_start_index.v + 1}; parser_token_i.v < parser_end_index.v; parser_token_i.v += 1)
    {
        let lexer_token_i = parser.parser_indices[parser_token_i.v];
        let token = parser.lexer_tokens[lexer_token_i.v];

        if (name_already_parsed && argument_parsed)
        {
            break;
        }

        switch (token.id)
        {
            break; case TokenId::LeftBracket:
            {
                ParserTokenIndex function_attribute_token_start_index = { parser_start_index.v + 1 };
                ParserTokenIndex symbol_attribute_token_start_index = { name_token_index.v + 1 };
                bool is_function_attribute = parser_token_i.v == function_attribute_token_start_index.v;
                bool is_symbol_attribute = parser_token_i.v == symbol_attribute_token_start_index.v;

                let right_bracket = next(parser, TokenId::RightBracket, parser_token_i, parser_end_index);
                if (right_bracket.v == token_not_found.v)
                {
                    BUSTER_TRAP();
                }

                BUSTER_CHECK(right_bracket.v - parser_token_i.v > 1);

                if (is_function_attribute)
                {
                    if (function_attributes_already_parsed)
                    {
                        BUSTER_TRAP();
                    }

                    function_attributes_already_parsed = true;

                    function_attributes = analyze_function_attributes(parser, function_attribute_token_start_index, right_bracket);
                }
                else if (is_symbol_attribute)
                {
                    if (!name_already_parsed)
                    {
                        BUSTER_TRAP();
                    }

                    if (symbol_attributes_already_parsed)
                    {
                        BUSTER_TRAP();
                    }

                    symbol_attributes_already_parsed = true;

                    symbol_attributes = analyze_symbol_attributes(parser, symbol_attribute_token_start_index, right_bracket);
                }
                else
                {
                    BUSTER_TRAP();
                }

                parser_token_i = right_bracket;
            }
            break; case TokenId::Identifier:
            {
                if (name_already_parsed)
                {
                    BUSTER_TRAP();
                }

                name_already_parsed = true;
                name_token_index = parser_token_i;
            }
            break; case TokenId::LeftParenthesis:
            {
                let right_parenthesis = next(parser, TokenId::RightParenthesis, parser_token_i, parser_end_index);
                if (right_parenthesis.v == token_not_found.v)
                {
                    BUSTER_TRAP();
                }

                analyze_argument_declarations(analyzer, parser, parser_token_i, right_parenthesis);

                argument_parsed = true;

                parser_token_i = right_parenthesis;
            }
            break; default: BUSTER_UNREACHABLE();
        }
    }

    analyze_type(analyzer, parser, parser_token_i, parser_end_index);
}

BUSTER_F_IMPL AnalysisResult analyze(ParserResult* restrict parsers, u64 parser_count)
{
    AnalysisResult result = {};
    Analyzer* analyzer = analyzer_initialize();
    bool a = true;

    for (u64 parser_i = 0; parser_i < parser_count; parser_i += 1)
    {
        ParserResult& parser = parsers[parser_i];

        for (u32 tld_i = 0; tld_i < parser.top_level_declaration_count; tld_i += 1)
        {
            const auto& tld = parser.top_level_declarations[tld_i];
            let start_index = tld.start;
            let end_index = tld.end;

            let parser_token_index = start_index; 
            let lexer_token_index = parser.parser_indices[parser_token_index.v];
            let token = parser.lexer_tokens[lexer_token_index.v];

            switch (token.id)
            {
                break; case TokenId::Keyword_Function:
                {
                    constexpr u32 mandatory_function_extra_tokens = 4;
                    ParserTokenIndex parser_finder_i;
                    for (parser_finder_i = { start_index.v + mandatory_function_extra_tokens }; parser_finder_i.v < end_index.v; parser_finder_i.v += 1)
                    {
                        let lexer_finder_i = parser.parser_indices[parser_finder_i.v];
                        let finder_token = parser.lexer_tokens[lexer_finder_i.v];
                        if (finder_token.id == TokenId::LeftBrace)
                        {
                            break;
                        }
                    }

                    BUSTER_CHECK(parser_finder_i.v != end_index.v);
                    let last_header_index = parser_finder_i;
                    analyze_function_header(analyzer, parser, start_index, last_header_index);
                }
                break; default: if (a) BUSTER_UNREACHABLE();
            }
        }
    }

    return result;
}

BUSTER_F_IMPL void analysis_experiments()
{
    Arena* arena = arena_create({});
    let source = BYTE_SLICE_TO_STRING(8, file_read(arena, SOs("tests/basic.bbb"), {}));
    let parser = parse(source.pointer, tokenize(arena, source.pointer, source.length));
    analyze(&parser, 1);
}
