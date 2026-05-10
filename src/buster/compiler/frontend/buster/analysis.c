#pragma once

#include <buster/compiler/frontend/buster/analysis.h>
#include <buster/file.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/integer.h>

// STRUCT(File)
// {
//     Arena* tld_arena;
// };
//
// STRUCT(Analyzer)
// {
//     IrModule* module;
//     Arena* file_arena;
// };
//
// // STRUCT(Cursor)
// // {
// //     ParserTokenIndex index;
// // };
// //
// BUSTER_GLOBAL_LOCAL Analyzer* analyzer_initialize()
// {
//     let arena = arena_create({});
//     let analyzer = arena_allocate(arena, Analyzer, 1);
//     *analyzer = {
//         .module = ir_module_create(arena_create({}), 0, S8("module")),
//         .file_arena = arena_create({}),
//     };
//     return analyzer;
// }
//
// BUSTER_GLOBAL_LOCAL String8 function_attribute_names[] = {
//     [(u64)IrFunctionAttribute::CallingConvention] = S8("cc"),
// };
//
// static_assert(BUSTER_ARRAY_LENGTH(function_attribute_names) == (u64)IrFunctionAttribute::Count);
//
// BUSTER_GLOBAL_LOCAL String8 calling_convention_names[] = {
//     [(u64)IrCallingConvention::C] = S8("c"),
//     [(u64)IrCallingConvention::SystemV] = S8("systemv"),
//     [(u64)IrCallingConvention::Win64] = S8("win64"),
// };
//
// static_assert(BUSTER_ARRAY_LENGTH(calling_convention_names) == (u64)IrCallingConvention::Count);
//
// // BUSTER_GLOBAL_LOCAL ParserTokenIndex token_not_found = { UINT32_MAX };
//
// // BUSTER_GLOBAL_LOCAL ParserTokenIndex next(const ParserResult& parser, TokenId id, ParserTokenIndex start, ParserTokenIndex end)
// // {
// //     ParserTokenIndex result = token_not_found;
// //
// //     for (ParserTokenIndex parser_token_i = start; parser_token_i.v < end.v; parser_token_i.v += 1)
// //     {
// //         let lexer_token_i = parser.parser_to_lexer_indices[parser_token_i.v];
// //         let token = parser.lexer_tokens[lexer_token_i.v];
// //         if (token.id == id)
// //         {
// //             result = parser_token_i;
// //             break;
// //         }
// //     }
// //
// //     return result;
// // }
//
// // BUSTER_GLOBAL_LOCAL IrFunctionAttributes analyze_function_attributes(const ParserResult& parser, ParserTokenIndex left_bracket_parser_index, ParserTokenIndex right_bracket_parser_index)
// // {
// //     IrFunctionAttributes attributes = {};
// //     Slice<String8> function_attribute_name_array = BUSTER_ARRAY_TO_SLICE(function_attribute_names);
// //     Slice<String8> calling_convention_name_array = BUSTER_ARRAY_TO_SLICE(calling_convention_names);
// //
// //     for (ParserTokenIndex parser_token_i = { left_bracket_parser_index.v + 1 }; parser_token_i.v < right_bracket_parser_index.v; parser_token_i.v += 1)
// //     {
// //         let lexer_token_i = parser.parser_to_lexer_indices[parser_token_i.v];
// //         let token = &parser.lexer_tokens[lexer_token_i.v];
// //
// //         if (token->id == TokenId::Identifier)
// //         {
// //             let attribute_identifier = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i);
// //
// //             let function_attribute_i = string8_array_match(function_attribute_name_array, attribute_identifier);
// //
// //             if (function_attribute_i != BUSTER_STRING_NO_MATCH)
// //             {
// //                 let attribute = (IrFunctionAttribute)function_attribute_i;
// //
// //                 switch (attribute)
// //                 {
// //                     break; case IrFunctionAttribute::CallingConvention:
// //                     {
// //                         ParserTokenIndex next_parser_token_i = { .v = parser_token_i.v  + 1 };
// //
// //                         if (next_parser_token_i.v < right_bracket_parser_index.v)
// //                         {
// //                             lexer_token_i = parser.parser_to_lexer_indices[next_parser_token_i.v];
// //                             token = &parser.lexer_tokens[lexer_token_i.v];
// //
// //                             if (token->id == TokenId::LeftParenthesis)
// //                             {
// //                                 next_parser_token_i.v += 1;
// //                                 if (next_parser_token_i.v >= right_bracket_parser_index.v)
// //                                 {
// //                                     BUSTER_TRAP();
// //                                 }
// //
// //                                 lexer_token_i = parser.parser_to_lexer_indices[next_parser_token_i.v];
// //                                 token = &parser.lexer_tokens[lexer_token_i.v];
// //                                 if (token->id != TokenId::Identifier)
// //                                 {
// //                                     BUSTER_TRAP();
// //                                 }
// //
// //                                 let calling_convention_name = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i);
// //                                 let calling_convention_i = string8_array_match(calling_convention_name_array, calling_convention_name);
// //                                 if (calling_convention_i == BUSTER_STRING_NO_MATCH)
// //                                 {
// //                                     BUSTER_TRAP();
// //                                 }
// //
// //                                 attributes.calling_convention = (IrCallingConvention)calling_convention_i;
// //
// //                                 next_parser_token_i.v += 1;
// //                                 if (next_parser_token_i.v >= right_bracket_parser_index.v)
// //                                 {
// //                                     BUSTER_TRAP();
// //                                 }
// //
// //                                 lexer_token_i = parser.parser_to_lexer_indices[next_parser_token_i.v];
// //                                 token = &parser.lexer_tokens[lexer_token_i.v];
// //                                 if (token->id != TokenId::RightParenthesis)
// //                                 {
// //                                     BUSTER_TRAP();
// //                                 }
// //
// //                                 parser_token_i = next_parser_token_i;
// //                             }
// //                             else if (token->id == TokenId::Identifier)
// //                             {
// //                                 let calling_convention_name = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i);
// //                                 let calling_convention_i = string8_array_match(calling_convention_name_array, calling_convention_name);
// //                                 if (calling_convention_i != BUSTER_STRING_NO_MATCH)
// //                                 {
// //                                     attributes.calling_convention = (IrCallingConvention)calling_convention_i;
// //                                     parser_token_i = next_parser_token_i;
// //                                 }
// //                                 else
// //                                 {
// //                                     BUSTER_TRAP();
// //                                 }
// //
// //                             }
// //                         }
// //                     }
// //                     break; default:
// //                     {
// //                         BUSTER_UNREACHABLE();
// //                     }
// //                 }
// //             }
// //             else
// //             {
// //                 BUSTER_TRAP();
// //             }
// //         }
// //         else
// //         {
// //             BUSTER_TRAP();
// //         }
// //     }
// //
// //     return attributes;
// // }
//
//
// BUSTER_GLOBAL_LOCAL IrSymbolAttributes analyze_symbol_attributes(const ParserResult& parser, ParserTokenIndex left_bracket_parser_index, ParserTokenIndex right_bracket_parser_index)
// {
//     IrSymbolAttributes attributes = {};
//     Slice<String8> symbol_attribute_name_array = BUSTER_ARRAY_TO_SLICE(symbol_attribute_names);
//
//     for (ParserTokenIndex parser_token_i = { .v = left_bracket_parser_index.v + 1 }; parser_token_i.v < right_bracket_parser_index.v; parser_token_i.v += 1)
//     {
//         let lexer_token_i = parser.parser_to_lexer_indices[parser_token_i.v];
//         let token = &parser.lexer_tokens[lexer_token_i.v];
//
//         if (token->id == TokenId::Identifier)
//         {
//             let attribute_identifier = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i);
//
//             let symbol_attribute_i = string8_array_match(symbol_attribute_name_array, attribute_identifier);
//
//             if (symbol_attribute_i != BUSTER_STRING_NO_MATCH)
//             {
//                 let attribute = (IrSymbolAttribute)symbol_attribute_i;
//
//                 switch (attribute)
//                 {
//                     break; case IrSymbolAttribute::Export:
//                     {
//                         attributes.exported = true;
//                         attributes.linkage = IrLinkage::External;
//                     }
//                     break; case IrSymbolAttribute::Count: BUSTER_UNREACHABLE();
//                 }
//             }
//             else
//             {
//                 BUSTER_TRAP();
//             }
//         }
//         else
//         {
//             BUSTER_TRAP();
//         }
//     }
//
//     return attributes;
// }
//
// STRUCT(IrTypePair)
// {
//     IrTypeRef ir;
//     IrDebugTypeRef debug;
// };
//
// BUSTER_GLOBAL_LOCAL IrTypePair analyze_type(Analyzer* analyzer, const ParserResult& parser, ParserTokenIndex type_start, ParserTokenIndex type_end)
// {
//     IrTypePair result = {};
//
//     let parser_token_i = type_start;
//     let lexer_token_i = parser.parser_to_lexer_indices[parser_token_i.v];
//     let token = parser.lexer_tokens[lexer_token_i.v];
//
//     switch (token.id)
//     {
//         break; case TokenId::Ampersand:
//         {
//             let element_type_pair = analyze_type(analyzer, parser, { type_start.v + 1 }, type_end);
//             result = {
//                 .ir = ir_type_get_builtin(analyzer->module, IrTypeId::pointer),
//                 .debug = ir_debug_type_get_pointer(analyzer->module, element_type_pair.debug),
//             };
//         }
//         break; case TokenId::Identifier:
//         {
//             let identifier = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i);
//
//             bool is_builtin = false;
//
//             if (identifier.length >= 2)
//             {
//                 bool is_decimal_after_first_digit = true;
//                 String8 decimal_slice = string8_slice(identifier, 1, identifier.length);
//
//                 for (u64 i = 1; i < identifier.length; i += 1)
//                 {
//                     let ch = identifier.pointer[i];
//                     if (!code_unit8_is_decimal(ch))
//                     {
//                         is_decimal_after_first_digit = false;
//                         break;
//                     }
//                 }
//
//                 if (is_decimal_after_first_digit)
//                 {
//                     let bit_count = string8_parse_u64_decimal(decimal_slice.pointer).value;
//
//                     bool is_unsigned = identifier.pointer[0] == 'u';
//                     bool is_signed = identifier.pointer[0] == 's';
//
//                     if (is_unsigned || is_signed)
//                     {
//                         if ((bit_count > 0 && bit_count <= 64) || bit_count == 128)
//                         {
//                             result = {
//                                 .ir = ir_type_get_builtin(analyzer->module, (IrTypeId)(bit_count - 1)),
//                                 .debug = ir_debug_type_get_builtin(analyzer->module, (IrDebugTypeId)((bit_count - 1) + (is_signed * (u64)IrDebugTypeId::s1))),
//                             };
//                             is_builtin = true;
//                         }
//                         else
//                         {
//                             BUSTER_TRAP();
//                         }
//                     }
//                     else if (identifier.pointer[0] == 'f')
//                     {
//                         BUSTER_TRAP();
//                     }
//                 }
//             }
//
//             if (!is_builtin)
//             {
//                 BUSTER_TRAP();
//             }
//         }
//         break; default: BUSTER_UNREACHABLE();
//     }
//
//     return result;
// }
//
// BUSTER_GLOBAL_LOCAL IrFunctionRef analyze_function_header(Analyzer* analyzer, const ParserResult& parser, ParserTokenIndex parser_start_index, ParserTokenIndex parser_end_index)
// {
//     bool function_attributes_already_parsed = false;
//     bool symbol_attributes_already_parsed = false;
//     bool name_already_parsed = false;
//     ParserTokenIndex name_token_index = { token_not_found.v - 1 };
//     bool argument_parsed = false;
//
//     IrFunctionAttributes function_attributes = {};
//     IrSymbolAttributes symbol_attributes = {};
//
//     Target* function_target = 0;
//
//     let argument_scratch = scratch_begin(0, 0);
//     defer { scratch_end(argument_scratch); };
//
//     ParserTokenIndex parser_token_i;
//
//     STRUCT(ArgumentInformation)
//     {
//         IrTypePair type_pair;
//         IrInternRef name;
//     };
//
//     Slice<ArgumentInformation> argument_informations = {};
//     IrInternRef function_name = {};
//
//     // TODO: we should unroll this loop
//     
//     for (parser_token_i = {parser_start_index.v + 1}; parser_token_i.v < parser_end_index.v; parser_token_i.v += 1)
//     {
//         let lexer_token_i = parser.parser_to_lexer_indices[parser_token_i.v];
//         let token = parser.lexer_tokens[lexer_token_i.v];
//
//         if (name_already_parsed && argument_parsed)
//         {
//             break;
//         }
//
//         switch (token.id)
//         {
//             break; case TokenId::LeftBracket:
//             {
//                 ParserTokenIndex function_attribute_token_start_index = { parser_start_index.v + 1 };
//                 ParserTokenIndex symbol_attribute_token_start_index = { name_token_index.v + 1 };
//                 bool is_function_attribute = parser_token_i.v == function_attribute_token_start_index.v;
//                 bool is_symbol_attribute = parser_token_i.v == symbol_attribute_token_start_index.v;
//
//                 let right_bracket = next(parser, TokenId::RightBracket, parser_token_i, parser_end_index);
//                 if (right_bracket.v == token_not_found.v)
//                 {
//                     BUSTER_TRAP();
//                 }
//
//                 BUSTER_CHECK(right_bracket.v - parser_token_i.v > 1);
//
//                 if (is_function_attribute)
//                 {
//                     if (function_attributes_already_parsed)
//                     {
//                         BUSTER_TRAP();
//                     }
//
//                     function_attributes_already_parsed = true;
//
//                     function_attributes = analyze_function_attributes(parser, function_attribute_token_start_index, right_bracket);
//                 }
//                 else if (is_symbol_attribute)
//                 {
//                     if (!name_already_parsed)
//                     {
//                         BUSTER_TRAP();
//                     }
//
//                     if (symbol_attributes_already_parsed)
//                     {
//                         BUSTER_TRAP();
//                     }
//
//                     symbol_attributes_already_parsed = true;
//
//                     symbol_attributes = analyze_symbol_attributes(parser, symbol_attribute_token_start_index, right_bracket);
//                 }
//                 else
//                 {
//                     BUSTER_TRAP();
//                 }
//
//                 parser_token_i = right_bracket;
//             }
//             break; case TokenId::Identifier:
//             {
//                 if (name_already_parsed)
//                 {
//                     BUSTER_TRAP();
//                 }
//
//                 name_already_parsed = true;
//                 name_token_index = parser_token_i;
//                 let name_lexer_token_i = parser.parser_to_lexer_indices[name_token_index.v];
//                 let function_name_string = get_token_content(parser.source, parser.lexer_tokens, name_lexer_token_i);
//                 function_name = ir_module_intern(analyzer->module, function_name_string); 
//             }
//             break; case TokenId::LeftParenthesis:
//             {
//                 let right_parenthesis = next(parser, TokenId::RightParenthesis, parser_token_i, parser_end_index);
//                 if (right_parenthesis.v == token_not_found.v)
//                 {
//                     BUSTER_TRAP();
//                 }
//
//                 let left_parenthesis = parser_token_i;
//                 parser_token_i = { left_parenthesis.v + 1 };
//
//                 u32 argument_count = 0;
//                 ArgumentInformation* first_argument = 0;
//
//                 while (parser_token_i.v < right_parenthesis.v)
//                 {
//                     let argument_information = arena_allocate(argument_scratch.arena, ArgumentInformation, 1);
//                     first_argument = first_argument ? first_argument : argument_information;
//
//                     {
//                         u32 i = 0;
//                         ParserTokenIndex parser_i = { parser_token_i.v + i };
//                         let lexer_i = parser.parser_to_lexer_indices[parser_i.v];
//                         let colon_token = parser.lexer_tokens[lexer_i.v];
//                         BUSTER_CHECK(colon_token.id == TokenId::Colon);
//                     }
//
//                     {
//                         u32 i = 1;
//                         ParserTokenIndex parser_i = { parser_token_i.v + i };
//                         let lexer_i = parser.parser_to_lexer_indices[parser_i.v];
//                         let identifier_token = parser.lexer_tokens[lexer_i.v];
//                         BUSTER_CHECK(identifier_token.id == TokenId::Identifier);
//                         let argument_name = get_token_content(parser.source, parser.lexer_tokens, lexer_i);
//                         argument_information->name = ir_module_intern(analyzer->module, argument_name); 
//                     }
//
//                     let comma = next(parser, TokenId::Comma, parser_token_i, right_parenthesis);
//                     let argument_end = comma.v == token_not_found.v ? right_parenthesis : comma;
//
//                     argument_information->type_pair = analyze_type(analyzer, parser, { parser_token_i.v + 2 }, argument_end);
//
//                     parser_token_i = { argument_end.v + (argument_end.v == comma.v) };
//
//                     argument_count += 1;
//                 }
//
//                 argument_informations.pointer = first_argument;
//                 argument_informations.length = argument_count;
//
//                 argument_parsed = true;
//
//                 parser_token_i = right_parenthesis;
//             }
//             break; default: BUSTER_UNREACHABLE();
//         }
//     }
//
//     let return_type = analyze_type(analyzer, parser, parser_token_i, parser_end_index);
//
//     let argument_names = ir_module_allocate_name_array(analyzer->module, argument_informations.length);
//     let argument_debug_types = ir_module_allocate_debug_type_array(analyzer->module, argument_informations.length);
//     let semantic_argument_types = ir_module_allocate_type_array(analyzer->module, argument_informations.length);
//
//     for (u64 i = 0; i < argument_informations.length; i += 1)
//     {
//         argument_names[i] = argument_informations[i].name;
//         argument_debug_types[i] = argument_informations[i].type_pair.debug;
//         semantic_argument_types[i] = argument_informations[i].type_pair.ir;
//     }
//
//     let ir_debug_function_type = ir_debug_type_get_function(analyzer->module, {
//         .return_type = return_type.debug,
//         .argument_types = argument_debug_types,
//         .attributes = function_attributes,
//     });
//
//     let ir_function_type = ir_type_get_function(analyzer->module, {
//         .debug_type = ir_debug_function_type,
//         .return_type = return_type.ir,
//         .argument_types = semantic_argument_types,
//         .target = function_target,
//         .attributes = function_attributes,
//     });
//
//     let function = ir_function_create(analyzer->module, IrFunctionCreate{
//         .ir_type = ir_function_type,
//         .debug_type = ir_debug_function_type,
//         .name = function_name,
//         .linkage = symbol_attributes.linkage,
//         .argument_names = argument_names.pointer,
//     });
//
//     return function;
// }
//
// ENUM_T(TopLevelDeclarationId, u8,
//         Function);
//
// STRUCT(TopLevelDeclaration)
// {
//     union
//     {
//         IrFunctionRef function;
//     };
//     TopLevelDeclarationId id;
//     u8 reserved[3];
// };
//
// BUSTER_GLOBAL_LOCAL IrValueRef analyze_expression(Analyzer* analyzer, const ParserResult& parser, File& file, Cursor& cursor, IrFunctionRef function_ref, IrTypePair expected)
// {
//     IrValueRef result = {};
//     BUSTER_UNUSED(file);
//     BUSTER_UNUSED(function_ref);
//     let lexer_token_i = parser.parser_to_lexer_indices[cursor.index.v];
//     let expression_start_token = parser.get_token(cursor.index);
//
//     switch (expression_start_token->id)
//     {
//         break; case TokenId::Number:
//         {
//             let number_string = get_token_content(parser.source, parser.lexer_tokens, lexer_token_i);
//             let number_parsing = string8_parse_u64_decimal(number_string.pointer);
//             let value = number_parsing.value;
//
//             if (expected.ir.is_valid())
//             {
//                 let expected_ir_type = ir_type_get(analyzer->module, expected.ir);
//                 switch (expected_ir_type->id)
//                 {
//                     break;
//                     case IrTypeId::i1:
//                     case IrTypeId::i2:
//                     case IrTypeId::i3:
//                     case IrTypeId::i4:
//                     case IrTypeId::i5:
//                     case IrTypeId::i6:
//                     case IrTypeId::i7:
//                     case IrTypeId::i8:
//                     case IrTypeId::i9:
//                     case IrTypeId::i10:
//                     case IrTypeId::i11:
//                     case IrTypeId::i12:
//                     case IrTypeId::i13:
//                     case IrTypeId::i14:
//                     case IrTypeId::i15:
//                     case IrTypeId::i16:
//                     case IrTypeId::i17:
//                     case IrTypeId::i18:
//                     case IrTypeId::i19:
//                     case IrTypeId::i20:
//                     case IrTypeId::i21:
//                     case IrTypeId::i22:
//                     case IrTypeId::i23:
//                     case IrTypeId::i24:
//                     case IrTypeId::i25:
//                     case IrTypeId::i26:
//                     case IrTypeId::i27:
//                     case IrTypeId::i28:
//                     case IrTypeId::i29:
//                     case IrTypeId::i30:
//                     case IrTypeId::i31:
//                     case IrTypeId::i32:
//                     case IrTypeId::i33:
//                     case IrTypeId::i34:
//                     case IrTypeId::i35:
//                     case IrTypeId::i36:
//                     case IrTypeId::i37:
//                     case IrTypeId::i38:
//                     case IrTypeId::i39:
//                     case IrTypeId::i40:
//                     case IrTypeId::i41:
//                     case IrTypeId::i42:
//                     case IrTypeId::i43:
//                     case IrTypeId::i44:
//                     case IrTypeId::i45:
//                     case IrTypeId::i46:
//                     case IrTypeId::i47:
//                     case IrTypeId::i48:
//                     case IrTypeId::i49:
//                     case IrTypeId::i50:
//                     case IrTypeId::i51:
//                     case IrTypeId::i52:
//                     case IrTypeId::i53:
//                     case IrTypeId::i54:
//                     case IrTypeId::i55:
//                     case IrTypeId::i56:
//                     case IrTypeId::i57:
//                     case IrTypeId::i58:
//                     case IrTypeId::i59:
//                     case IrTypeId::i60:
//                     case IrTypeId::i61:
//                     case IrTypeId::i62:
//                     case IrTypeId::i63:
//                     case IrTypeId::i64:
//                     {
//                         let bit_count = (u64)expected_ir_type->id - 1;
//                         let max_value = bit_count == 64 ? UINT64_MAX : ((u64)1 << bit_count) - 1;
//
//                         if (value > max_value)
//                         {
//                             BUSTER_TRAP();
//                         }
//
//                         let value_ref = ir_get_constant_integer(analyzer->module, expected.ir, value);
//
//                         result = value_ref;
//                     }
//                     break; case IrTypeId::i128:
//                     case IrTypeId::Void:
//                     case IrTypeId::pointer:
//                     case IrTypeId::hf16:
//                     case IrTypeId::bf16:
//                     case IrTypeId::f32:
//                     case IrTypeId::f64:
//                     case IrTypeId::f128:
//                     case IrTypeId::v64:
//                     case IrTypeId::v128:
//                     case IrTypeId::v256:
//                     case IrTypeId::v512:
//                     case IrTypeId::vector:
//                     case IrTypeId::aggregate:
//                     case IrTypeId::function:
//                     case IrTypeId::Count:
//                     {
//                         BUSTER_UNREACHABLE();
//                     }
//                     // break; default: BUSTER_UNREACHABLE();
//                 }
//             }
//             else
//             {
//                 BUSTER_TRAP();
//             }
//
//             cursor.index.v += 1;
//         }
//         break; default: if (analyzer) BUSTER_UNREACHABLE();
//     }
//
//     return result;
// }
//
// BUSTER_GLOBAL_LOCAL void analyze_statement(Analyzer* analyzer, const ParserResult& parser, File& file, Cursor& restrict cursor, IrFunctionRef function_ref, IrBlockRef block_ref)
// {
//     let statement_start_lexer_token_i = parser.parser_to_lexer_indices[cursor.index.v];
//     let statement_start_token = parser.lexer_tokens[statement_start_lexer_token_i.v];
//     BUSTER_CHECK(statement_start_token.id == TokenId::Semicolon);
//
//     cursor.index.v += 1;
//
//     let lexer_token_i = parser.parser_to_lexer_indices[cursor.index.v];
//     let token = parser.lexer_tokens[lexer_token_i.v];
//
//     switch (token.id)
//     {
//         break; case TokenId::Keyword_Return:
//         {
//             let function = ir_function_get(analyzer->module, function_ref);
//             let ir_function_type = ir_type_get(analyzer->module, function->declaration.symbol.type);
//             BUSTER_CHECK(ir_function_type->id == IrTypeId::function);
//             let ir_return_type = ir_function_type->function.semantic.return_type;
//             let debug_function_type = ir_debug_type_get(analyzer->module, function->declaration.symbol.debug_type);
//             let debug_return_type = debug_function_type->function.return_type;
//             IrTypePair return_type_pair = { .ir = ir_return_type, debug_return_type };
//             cursor.index.v += 1;
//             let return_value = analyze_expression(analyzer, parser, file, cursor, function_ref, return_type_pair);
//             // ir_create_statement(analyzer->module,
//             //         IrStatement
//             //         {
//             //             // .unary = return_value,
//             //             .id = IrStatementId::Return,
//             //         }, block_ref);
//         }
//         break; default: BUSTER_UNREACHABLE();
//     }
// }
//
// BUSTER_GLOBAL_LOCAL IrBlockRef analyze_block(Analyzer* analyzer, const ParserResult& parser, File& file, IrFunctionRef function_ref, ParserTokenIndex left_brace, ParserTokenIndex right_brace)
// {
//     ParserTokenIndex body_range[] = { left_brace, right_brace };
//
//     for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(body_range); i += 1)
//     {
//         let parser_token_i = body_range[i];
//         let lexer_token_i = parser.parser_to_lexer_indices[parser_token_i.v];
//         let token = parser.lexer_tokens[lexer_token_i.v];
//         BUSTER_CHECK(token.id == (TokenId)((u64)TokenId::LeftBrace + i));
//     }
//
//     let result = ir_create_block(analyzer->module);
//
//     for (ParserTokenIndex statement_start = { left_brace.v + 1 }; statement_start.v < right_brace.v; statement_start.v += 1)
//     {
//         let lexer_token_i = parser.parser_to_lexer_indices[statement_start.v];
//         let token = parser.lexer_tokens[lexer_token_i.v];
//
//         Cursor statement_cursor = { statement_start };
//
//         switch (token.id)
//         {
//             break; case TokenId::Semicolon: analyze_statement(analyzer, parser, file, statement_cursor, function_ref, result);
//             break; default: BUSTER_UNREACHABLE();
//         }
//
//         statement_start = statement_cursor.index;
//     }
//
//     return result;
// }
//
// BUSTER_GLOBAL_LOCAL IrBlockRef analyze_function_body(Analyzer* analyzer, const ParserResult& parser, File& file, IrFunctionRef function_ref, TopLevelDeclarationRange range)
// {
//     let left_brace = range.first_body_index;
//     ParserTokenIndex right_brace = { .v = range.end.v - 1 };
//
//     let block_ref = analyze_block(analyzer, parser, file, function_ref, left_brace, right_brace);
//     return block_ref;
// }
//
// BUSTER_F_IMPL AnalysisResult analyze(ParserResult* restrict parsers, u64 file_count)
// {
//     AnalysisResult result = {};
//     Analyzer* analyzer = analyzer_initialize();
//
//     Slice<File> files = { arena_allocate(analyzer->file_arena, File, file_count), file_count };
//     for (u64 parser_i = 0; parser_i < file_count; parser_i += 1)
//     {
//         const ParserResult& parser = parsers[parser_i];
//
//         File* file = &files[parser_i];
//         *file = {
//             .tld_arena = arena_create({}),
//         };
//
//         let tlds = arena_allocate(file->tld_arena, TopLevelDeclaration, parser.top_level_declaration_count);
//
//         for (u32 tld_i = 0; tld_i < parser.top_level_declaration_count; tld_i += 1)
//         {
//             let tld = &tlds[tld_i];
//             auto& tld_range = parser.top_level_declarations[tld_i];
//             let start_index = tld_range.start;
//             let end_index = tld_range.end;
//
//             let parser_token_index = start_index; 
//             let lexer_token_index = parser.parser_to_lexer_indices[parser_token_index.v];
//             let token = parser.lexer_tokens[lexer_token_index.v];
//
//             switch (token.id)
//             {
//                 break; case TokenId::Keyword_Function:
//                 {
//                     constexpr u32 mandatory_function_extra_tokens = 4;
//                     ParserTokenIndex parser_finder_i;
//                     for (parser_finder_i = { start_index.v + mandatory_function_extra_tokens }; parser_finder_i.v < end_index.v; parser_finder_i.v += 1)
//                     {
//                         let lexer_finder_i = parser.parser_to_lexer_indices[parser_finder_i.v];
//                         let finder_token = parser.lexer_tokens[lexer_finder_i.v];
//                         if (finder_token.id == TokenId::LeftBrace)
//                         {
//                             break;
//                         }
//                     }
//
//                     BUSTER_CHECK(parser_finder_i.v != end_index.v);
//                     let first_body_index = parser_finder_i;
//                     tld_range.first_body_index = first_body_index;
//                     let function = analyze_function_header(analyzer, parser, start_index, first_body_index);
//                     tld->function = function;
//                     tld->id = TopLevelDeclarationId::Function;
//                 }
//                 break; default: BUSTER_UNREACHABLE();
//             }
//         }
//     }
//
//     for (u64 file_i = 0; file_i < file_count; file_i += 1)
//     {
//         File& file = files[file_i];
//         const ParserResult& parser = parsers[file_i];
//
//         let tlds = arena_get_slice_at_position(file.tld_arena, TopLevelDeclaration, arena_minimum_position, file.tld_arena->position);
//
//         for (u32 tld_i = 0; tld_i < parser.top_level_declaration_count; tld_i += 1)
//         {
//             const auto& tld_range = parser.top_level_declarations[tld_i];
//             const auto& tld = tlds[tld_i];
//
//             switch (tld.id)
//             {
//                 break; case TopLevelDeclarationId::Function:
//                 {
//                     analyze_function_body(analyzer, parser, file, tld.function, tld_range);
//                 }
//                 break; case TopLevelDeclarationId::Count: BUSTER_UNREACHABLE();
//             }
//         }
//     }
//
//     return result;
// }
//
// BUSTER_F_IMPL void analysis_experiments()
// {
//     Arena* arena = arena_create({});
//     let source = BYTE_SLICE_TO_STRING(8, file_read(arena, SOs("tests/basic.bbb"), {}));
//     let parser = parse(source.pointer, tokenize(arena, source.pointer, source.length));
//     analyze(&parser, 1);
// }
