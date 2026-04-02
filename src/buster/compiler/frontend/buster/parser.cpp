#pragma once
#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/file.h>
#include <buster/arguments.h>
#include <buster/compiler/ir/ir.h>

BUSTER_GLOBAL_LOCAL constexpr u64 keyword_count = (u64)TokenId::Keyword_Last - (u64)TokenId::Keyword_First - 1;

BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
    [-1 + (u64)TokenId::Keyword_Function - (u64)TokenId::Keyword_First] = S8("fn"),
    [-1 + (u64)TokenId::Keyword_If - (u64)TokenId::Keyword_First] = S8("if"),
    [-1 + (u64)TokenId::Keyword_Else - (u64)TokenId::Keyword_First] = S8("else"),
    [-1 + (u64)TokenId::Keyword_Return - (u64)TokenId::Keyword_First] = S8("return"),
    [-1 + (u64)TokenId::Keyword_Let - (u64)TokenId::Keyword_First] = S8("let"),
    [-1 + (u64)TokenId::Keyword_For - (u64)TokenId::Keyword_First] = S8("for"),
    [-1 + (u64)TokenId::Keyword_While - (u64)TokenId::Keyword_First] = S8("while"),
};
static_assert(BUSTER_ARRAY_LENGTH(keyword_names) == keyword_count);

static_assert(sizeof(Token) == 2);

#define SWITCH_ALPHA_UPPER \
                case 'A':\
                case 'B':\
                case 'C':\
                case 'D':\
                case 'E':\
                case 'F':\
                case 'G':\
                case 'H':\
                case 'I':\
                case 'J':\
                case 'K':\
                case 'L':\
                case 'M':\
                case 'N':\
                case 'O':\
                case 'P':\
                case 'Q':\
                case 'R':\
                case 'S':\
                case 'T':\
                case 'U':\
                case 'V':\
                case 'X':\
                case 'Y':\
                case 'Z'

#define SWITCH_ALPHA_LOWER \
                case 'a':\
                case 'b':\
                case 'c':\
                case 'd':\
                case 'e':\
                case 'f':\
                case 'g':\
                case 'h':\
                case 'i':\
                case 'j':\
                case 'k':\
                case 'l':\
                case 'm':\
                case 'n':\
                case 'o':\
                case 'p':\
                case 'q':\
                case 'r':\
                case 's':\
                case 't':\
                case 'u':\
                case 'v':\
                case 'x':\
                case 'y':\
                case 'z'

#define SWITCH_DIGIT \
    case '0':\
    case '1':\
    case '2':\
    case '3':\
    case '4':\
    case '5':\
    case '6':\
    case '7':\
    case '8':\
    case '9'

BUSTER_F_IMPL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length)
{
    TokenizerResult result = {};

    if (file_length)
    {
        let token_start = arena_allocate(arena, Token, file_length + 2); // SOF, EOF
        let tokens = token_start;
        u64 token_count = 0;

        let it = file_pointer;
        let top = file_pointer + file_length;

        tokens[token_count++] = {
            .id = TokenId::SOF,
            .length = 1,
        };

        u32 line_count = 0;

        while (true)
        {
            if (it >= top)
            {
                break;
            }

            let start = it;
            let ch = *start;

            if (ch == '\n')
            {
                let line_offset = (u32)(it - file_pointer) + 1;
                let line_index = line_count++;

                let line_offset_token_count = ((u32)(line_offset > UINT8_MAX) << 1) + 1;
                let line_index_token_count = ((u32)(line_offset > UINT8_MAX) << 1) + 1;

                let line_offset_token_index = (u32)token_count;
                let line_index_token_index = (u32)(line_offset_token_index + line_offset_token_count);

                token_count = line_offset_token_index + line_offset_token_count + line_index_token_count;

                u32 values[] = { line_offset, line_index };
                u32 indices[] = { line_offset_token_index, line_index_token_index };
                TokenId ids[] = { TokenId::LineOffset, TokenId::LineIndex };

                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(values); i += 1)
                {
                    let token = &tokens[indices[i]];
                    let value = values[i];
                    *token = {
                        .id = ids[i],
                        .length = value > UINT8_MAX ? (u8)0 : (u8)value,
                    };

                    // We write here any way because why not
                    *(u32*)(token + 1) = value;
                }
            }

            {
                TokenId id;

                switch (ch)
                {
                    break;
SWITCH_ALPHA_UPPER:
SWITCH_ALPHA_LOWER:
                    case '_':
                    {
                        while (true)
                        {
                            let it_start = it;
                            switch (*it_start)
                            {
                                break;
SWITCH_ALPHA_UPPER:
SWITCH_ALPHA_LOWER:
SWITCH_DIGIT:
                                case '_':
                                {
                                    it += 1;
                                }
                                break; default: break;
                            }

                            if (it - it_start == 0)
                            {
                                break;
                            }
                        }

                        let identifier = string8_from_pointer_length(start, (u64)(it - start));

                        u64 i;
                        for (i = 0; i < keyword_count; i += 1)
                        {
                            if (string8_equal(identifier, keyword_names[i]))
                            {
                                break;
                            }
                        }

                        if (i == keyword_count)
                        {
                            id = TokenId::Identifier;
                        }
                        else
                        {
                            id = (TokenId)(i + 1 + (u64)TokenId::Keyword_First);
                        }
                    }
                    break; case ' ':
                    {
                        id = TokenId::Space;

                        while (*it == ' ')
                        {
                            it += 1;
                        }
                    }
                    break; SWITCH_DIGIT:
                    {
                        id = TokenId::Number;

                        while (true)
                        {
                            let it_start = it;

                            switch (*it_start)
                            {
                                break; SWITCH_DIGIT: it += 1;
                                break; default: break;
                            }

                            if (it - it_start == 0)
                            {
                                break;
                            }
                        }
                    }
                    break; case '\n': { id = TokenId::LineFeed; it += 1; }
                    break; case '[': { id = TokenId::LeftBracket; it += 1; }
                    break; case ']': { id = TokenId::RightBracket; it += 1; }
                    break; case '{': { id = TokenId::LeftBrace; it += 1; }
                    break; case '}': { id = TokenId::RightBrace; it += 1; }
                    break; case '(': { id = TokenId::LeftParenthesis; it += 1; }
                    break; case ')': { id = TokenId::RightParenthesis; it += 1; }
                    break; case '<': { id = TokenId::Less; it += 1; }
                    break; case '>': { id = TokenId::Greater; it += 1; }
                    break; case '+':
                    {
                        if (it + 1 < top && it[1] == '=')
                        {
                            id = TokenId::PlusEqual;
                            it += 2;
                        }
                        else
                        {
                            id = TokenId::Plus;
                            it += 1;
                        }
                    }
                    break; case '-': { id = TokenId::Minus; it += 1; }
                    break; case '*': { id = TokenId::Asterisk; it += 1; }
                    break; case '/': { id = TokenId::Slash; it += 1; }
                    break; case '=': { id = TokenId::Equal; it += 1; }
                    break; case ':': { id = TokenId::Colon; it += 1; }
                    break; case ';': { id = TokenId::Semicolon; it += 1; }
                    break; case ',': { id = TokenId::Comma; it += 1; }
                    break; case '&': { id = TokenId::Ampersand; it += 1; }
                    break; case '.':
                    {
                        if (it + 2 < top && it[1] == '.' && it[2] == '.')
                        {
                            id = TokenId::TripleDot;
                        }
                        else if (it + 1 < top && it[1] == '.')
                        {
                            id = TokenId::DoubleDot;
                        }
                        else
                        {
                            id = TokenId::Dot;
                        }

                        it += (u64)id - (u64)TokenId::Dot + 1;
                    }
                    break; default: BUSTER_UNREACHABLE();
                }

                let end = it;
                let length = (u32)(end - start);

                bool big_length = length > UINT8_MAX;

                let token_index = token_count;
                tokens[token_index] = { 
                    .id = id,
                    .length = BUSTER_SELECT(big_length, (u8)0, (u8)length),
                };

                static_assert(sizeof(Token) * 2 == sizeof(u32));

                *(u32*)(&tokens[token_index + 1]) = length;
                token_count = token_index + 1 + ((u32)big_length << 1);
            }
        }

        tokens[token_count++] = {
            .id = TokenId::EOF,
            .length = 1,
        };

        result.tokens.pointer = token_start;
        result.tokens.length = token_count;
    }

    return result;
}

// ============================================================
// Shunting-yard parser producing prefix (Polish notation) output.
//
// Following Validark's approach: the parse tree is a reordering
// of token indices into preorder. No AST nodes are allocated.
// Operands stay in original relative order; only operators are
// repositioned (moved before their operands).
//
// A rope (linked list of token indices) provides O(1) concatenation
// during the shunting-yard. Flattening the rope at the end yields
// the contiguous prefix token-index array.
// ============================================================

// ============================================================
// Rope: linked list of token indices for O(1) concat
// ============================================================

// constexpr u32 no_node = UINT32_MAX;
//
// STRUCT(RopeNode)
// {
//     LexerTokenIndex lexer_token_index;
//     u32 next; // index of next RopeNode, no_node = end
// };
//
// STRUCT(RopeRef)
// {
//     u32 head;
//     u32 tail;
// };

// constexpr RopeRef no_rope = { .head = no_node, .tail = no_node };

// STRUCT(RopeArena)
// {
//     Arena* arena;
//
//     BUSTER_INLINE u32 count()
//     {
//         return (u32)(arena_buffer_size(arena) / sizeof(RopeNode));
//     }
//
//     BUSTER_INLINE RopeNode* at(u32 index)
//     {
//         return arena_get_pointer_at_position(arena, RopeNode, arena_minimum_position + index * sizeof(RopeNode));
//     }
//
//     RopeRef make_leaf(LexerTokenIndex lexer_token_index)
//     {
//         u32 index = count();
//         let node = arena_allocate(arena, RopeNode, 1);
//         *node = { .lexer_token_index = lexer_token_index, .next = no_node };
//         RopeRef result = { .head = index, .tail = index };
//         return result;
//     }
//
//     // Concatenate: a then b
//     RopeRef concat(RopeRef a, RopeRef b)
//     {
//         RopeRef result;
//         if (a.head == no_node) { result = b; }
//         else if (b.head == no_node) { result = a; }
//         else
//         {
//             at(a.tail)->next = b.head;
//             result = { .head = a.head, .tail = b.tail };
//         }
//         return result;
//     }
//
//     // Prefix-combine binary: op -> left -> right
//     RopeRef combine_binary(LexerTokenIndex operator_lexer_token_index, RopeRef left, RopeRef right)
//     {
//         let op = make_leaf(operator_lexer_token_index);
//         at(op.tail)->next = left.head;
//         at(left.tail)->next = right.head;
//         RopeRef result = { .head = op.head, .tail = right.tail };
//         return result;
//     }
//
//     // Prefix-combine unary: op -> child
//     RopeRef combine_unary(LexerTokenIndex operator_lexer_token_index, RopeRef child)
//     {
//         let op = make_leaf(operator_lexer_token_index);
//         at(op.tail)->next = child.head;
//         RopeRef result = { .head = op.head, .tail = child.tail };
//         return result;
//     }
// };

// ============================================================
// Prefix output: flat array of lexer token indices in preorder
// ============================================================

// BUSTER_GLOBAL_LOCAL ParserResult flatten_top_level_declarations(
//     const char8* restrict source,
//     TokenizerResult tokenizer,
//     RopeArena* rope_arena,
//     Slice<RopeRef> top_level_declarations,
//     Arena* output,
//     u64 error_count)
// {
//     ParserTokenIndex count = {};
//     let declarations = arena_allocate(output, TopLevelDeclarationRange, top_level_declarations.length);
//     let parser_to_lexer_indices = arena_current_pointer(output, LexerTokenIndex);
//
//     for (u32 i = 0; i < top_level_declarations.length; i += 1)
//     {
//         declarations[i].start = count;
//
//         u32 current = top_level_declarations.pointer[i].head;
//         while (current != no_node)
//         {
//             let node = rope_arena->at(current);
//             *arena_allocate(output, LexerTokenIndex, 1) = node->lexer_token_index;
//             count.v += 1;
//             current = node->next;
//         }
//
//         declarations[i].end = count;
//         // TODO?
//         declarations[i].first_body_index = count;
//     }
//
//     ParserResult result = {
//         .parser_to_lexer_indices = parser_to_lexer_indices,
//         .top_level_declarations = declarations,
//         .lexer_tokens = tokenizer.tokens.pointer,
//         .parser_token_count = count.v,
//         .top_level_declaration_count = (u32)top_level_declarations.length,
//         .lexer_token_count = (u32)tokenizer.tokens.length,
//         .error_count = error_count,
//         .source = source,
//     };
//     return result;
// }

// ============================================================
// Operator precedence
// ============================================================

// BUSTER_GLOBAL_LOCAL u8 infix_precedence(TokenId id)
// {
//     u8 result;
//     switch (id)
//     {
//         break; case TokenId::Equal:      result = 1;
//         break; case TokenId::PlusEqual:  result = 1;
//         break; case TokenId::Colon:      result = 2;
//         break; case TokenId::Greater:    result = 3;
//         break; case TokenId::Less:       result = 3;
//         break; case TokenId::Plus:       result = 4;
//         break; case TokenId::Minus:      result = 4;
//         break; case TokenId::Asterisk:   result = 5;
//         break; case TokenId::Slash:      result = 5;
//         break; case TokenId::Percentage: result = 5;
//         break; default: result = 0;
//     }
//     return result;
// }

// BUSTER_GLOBAL_LOCAL bool token_is_right_associative(TokenId id)
// {
//     bool result;
//     switch (id)
//     {
//         break; case TokenId::Equal: result = true;
//         break; case TokenId::PlusEqual: result = true;
//         break; default: result = false;
//     }
//     return result;
// }

// ============================================================
// Shunting-yard stacks
// ============================================================

// STRUCT(OperatorEntry)
// {
//     LexerTokenIndex lexer_token_index;
//     u8 precedence;
//     u8 arity; // 1 = unary prefix, 2 = binary infix
//     bool right_associative;
//     bool is_sentinel; // for ( grouping
// };
//
// STRUCT(OperatorStack)
// {
//     Arena* arena;
//
//     BUSTER_INLINE void push(OperatorEntry entry)
//     {
//         *arena_allocate(arena, OperatorEntry, 1) = entry;
//     }
//
//     BUSTER_INLINE bool is_empty()
//     {
//         return arena_buffer_is_empty(arena);
//     }
//
//     BUSTER_INLINE OperatorEntry top()
//     {
//         return *arena_get_pointer_at_position(arena, OperatorEntry, arena->position - sizeof(OperatorEntry));
//     }
//
//     BUSTER_INLINE OperatorEntry pop()
//     {
//         let position = arena->position - sizeof(OperatorEntry);
//         let result = *arena_get_pointer_at_position(arena, OperatorEntry, position);
//         arena->position = position;
//         return result;
//     }
// };

// STRUCT(OperandStack)
// {
//     Arena* arena;
//
//     BUSTER_INLINE void push(RopeRef ref)
//     {
//         *arena_allocate(arena, RopeRef, 1) = ref;
//     }
//
//     BUSTER_INLINE bool is_empty()
//     {
//         return arena_buffer_is_empty(arena);
//     }
//
//     BUSTER_INLINE RopeRef pop()
//     {
//         let position = arena->position - sizeof(RopeRef);
//         let result = *arena_get_pointer_at_position(arena, RopeRef, position);
//         arena->position = position;
//         return result;
//     }
// };

// ============================================================
// Shunting-yard parser state
// ============================================================

// STRUCT(ShuntingYard)
// {
//     // Input
//     Token* restrict tokens;
//     u32 token_count;
//     LexerTokenIndex index;
//
//     // Rope output
//     RopeArena rope;
//
//     // Shunting-yard stacks
//     OperatorStack operators;
//     OperandStack operands;
//     u64 error_count;
//
//     BUSTER_INLINE bool at_end()
//     {
//         return index.v >= token_count;
//     }
//
//     // Skip whitespace, advancing index
//     void skip_whitespace()
//     {
//         while (index.v < token_count)
//         {
//             let id = tokens[index.v].id;
//             if (id == TokenId::Space || id == TokenId::LineIndex || id == TokenId::LineOffset || id == TokenId::LineFeed || id == TokenId::CarriageReturn)
//             {
//                 advance();
//             }
//             else
//             {
//                 break;
//             }
//         }
//     }
//
//     // Peek at next significant token
//     Token peek()
//     {
//         skip_whitespace();
//         Token result = {};
//         if (index.v < token_count)
//         {
//             result = tokens[index.v];
//         }
//         return result;
//     }
//
//     // Advance past current token (handles big-length encoding)
//     void advance()
//     {
//         if (index.v < token_count)
//         {
//             u32 length = tokens[index.v].length;
//             if (length == 0)
//             {
//                 index.v += 3;
//             }
//             else
//             {
//                 index.v += 1;
//             }
//         }
//     }
//
//     // Pop top operator and combine with operands into a rope
//     void combine_top()
//     {
//         let op = operators.pop();
//
//         if (op.arity == 2)
//         {
//             let right = operands.pop();
//             let left = operands.pop();
//             operands.push(rope.combine_binary(op.lexer_token_index, left, right));
//         }
//         else if (op.arity == 1)
//         {
//             let child = operands.pop();
//             operands.push(rope.combine_unary(op.lexer_token_index, child));
//         }
//     }
//
//     // Drain operators with precedence >= threshold
//     void drain_operators(u8 precedence, bool right_associative)
//     {
//         while (!operators.is_empty())
//         {
//             let top = operators.top();
//             if (top.is_sentinel) { break; }
//
//             bool should_pop = false;
//             if (top.precedence > precedence)
//             {
//                 should_pop = true;
//             }
//             else if (top.precedence == precedence && !right_associative)
//             {
//                 should_pop = true;
//             }
//
//             if (!should_pop) { break; }
//             combine_top();
//         }
//     }
//
//     // Drain all non-sentinel operators
//     void drain_all()
//     {
//         while (!operators.is_empty())
//         {
//             if (operators.top().is_sentinel) { break; }
//             combine_top();
//         }
//     }
// };
//
// BUSTER_GLOBAL_LOCAL void sy_consume_disallowed_semicolons(ShuntingYard* restrict sy)
// {
//     let token = sy->peek();
//
//     while (token.id == TokenId::Semicolon)
//     {
//         sy->error_count += 1;
//         sy->advance();
//         token = sy->peek();
//     }
// }

// STRUCT(ParsedOperand)
// {
//     RopeRef rope;
//     TokenId tail_token_id;
//     u8 reserved[3];
// };
//
// // Forward declarations
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_bracket_group(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL ParsedOperand sy_parse_primary_operand(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_expression(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_attribute_list(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_argument_list(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_block(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_function(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_if(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_for(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_while(ShuntingYard* restrict sy);
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_statement(ShuntingYard* restrict sy);
//
// // ============================================================
// // Expression parsing — core shunting-yard
// //
// // Operands (identifiers, numbers) pass through in order.
// // Infix operators (+, -, *, /, %, >, <, =, :) are reordered
// // via the operator stack so they appear before their operands
// // in the output (prefix position).
// // Unary prefix operators (&, -, return, let) are handled the
// // same way with high precedence and arity 1.
// // Parentheses act as grouping with sentinels on the op stack.
// // ============================================================
//
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_bracket_group(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // [
//     sy->advance();
//
//     while (!sy->at_end())
//     {
//         let token = sy->peek();
//
//         if (token.id == TokenId::RightBracket)
//         {
//             result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//             sy->advance();
//             break;
//         }
//
//         LexerTokenIndex before = sy->index;
//         if (token.id == TokenId::LeftBracket)
//         {
//             result = sy->rope.concat(result, sy_parse_bracket_group(sy));
//         }
//         else
//         {
//             result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//             sy->advance();
//         }
//
//         if (sy->index.v == before.v)
//         {
//             sy->advance();
//         }
//     }
//
//     return result;
// }
//
// BUSTER_GLOBAL_LOCAL ParsedOperand sy_parse_primary_operand(ShuntingYard* restrict sy)
// {
//     ParsedOperand result = {
//         .rope = no_rope,
//         .tail_token_id = TokenId::Error,
//     };
//
//     let token = sy->peek();
//
//     switch (token.id)
//     {
//         break; case TokenId::Identifier:
//         {
//             result.rope = sy->rope.make_leaf(sy->index);
//             result.tail_token_id = TokenId::Identifier;
//             sy->advance();
//         }
//         break; case TokenId::Number:
//         {
//             result.rope = sy->rope.make_leaf(sy->index);
//             result.tail_token_id = TokenId::Number;
//             sy->advance();
//         }
//         break; case TokenId::LeftBracket:
//         {
//             result.rope = sy_parse_bracket_group(sy);
//             result.tail_token_id = TokenId::RightBracket;
//         }
//         break; default: break;
//     }
//
//     while (result.rope.head != no_node && !sy->at_end())
//     {
//         token = sy->peek();
//
//         bool should_consume_suffix = token.id == TokenId::LeftBracket;
//         should_consume_suffix = should_consume_suffix
//             || ((result.tail_token_id == TokenId::RightBracket)
//             && (token.id == TokenId::Identifier || token.id == TokenId::Number));
//
//         if (!should_consume_suffix)
//         {
//             break;
//         }
//
//         RopeRef suffix = no_rope;
//         switch (token.id)
//         {
//             break; case TokenId::LeftBracket:
//             {
//                 suffix = sy_parse_bracket_group(sy);
//                 result.tail_token_id = TokenId::RightBracket;
//             }
//             break; case TokenId::Identifier:
//             {
//                 suffix = sy->rope.make_leaf(sy->index);
//                 result.tail_token_id = TokenId::Identifier;
//                 sy->advance();
//             }
//             break; case TokenId::Number:
//             {
//                 suffix = sy->rope.make_leaf(sy->index);
//                 result.tail_token_id = TokenId::Number;
//                 sy->advance();
//             }
//             break; default: break;
//         }
//
//         result.rope = sy->rope.concat(result.rope, suffix);
//     }
//
//     return result;
// }
//
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_expression(ShuntingYard* restrict sy)
// {
//     bool expect_operand = true;
//     bool running = true;
//
//     while (running && !sy->at_end())
//     {
//         let token = sy->peek();
//         let lexer_token_index = sy->index;
//
//         if (expect_operand)
//         {
//             switch (token.id)
//             {
//                 break; case TokenId::Identifier:
//                 case TokenId::Number:
//                 case TokenId::LeftBracket:
//                 {
//                     let operand = sy_parse_primary_operand(sy);
//                     if (operand.rope.head != no_node)
//                     {
//                         sy->operands.push(operand.rope);
//                         expect_operand = false;
//                     }
//                 }
//                 break; case TokenId::LeftParenthesis:
//                 {
//                     // Grouping: push sentinel with ( token index
//                     sy->operators.push({
//                         .lexer_token_index = lexer_token_index,
//                         .precedence = 0,
//                         .arity = 0,
//                         .right_associative = false,
//                         .is_sentinel = true,
//                     });
//                     sy->advance();
//                 }
//                 break; case TokenId::Ampersand:
//                 {
//                     // Unary prefix: address-of
//                     sy->operators.push({
//                         .lexer_token_index = lexer_token_index,
//                         .precedence = 6,
//                         .arity = 1,
//                         .right_associative = true,
//                         .is_sentinel = false,
//                     });
//                     sy->advance();
//                 }
//                 break; case TokenId::Minus:
//                 {
//                     // Unary prefix: negation
//                     sy->operators.push({
//                         .lexer_token_index = lexer_token_index,
//                         .precedence = 6,
//                         .arity = 1,
//                         .right_associative = true,
//                         .is_sentinel = false,
//                     });
//                     sy->advance();
//                 }
//                 break; case TokenId::Keyword_Return:
//                 {
//                     // Prefix unary keyword (very low precedence)
//                     sy->operators.push({
//                         .lexer_token_index = lexer_token_index,
//                         .precedence = 0,
//                         .arity = 1,
//                         .right_associative = true,
//                         .is_sentinel = false,
//                     });
//                     sy->advance();
//                 }
//                 break; case TokenId::Keyword_Let:
//                 {
//                     // Prefix unary keyword (very low precedence)
//                     sy->operators.push({
//                         .lexer_token_index = lexer_token_index,
//                         .precedence = 0,
//                         .arity = 1,
//                         .right_associative = true,
//                         .is_sentinel = false,
//                     });
//                     sy->advance();
//                 }
//                 break; default:
//                 {
//                     // Not a valid operand start — end of expression
//                     running = false;
//                 }
//             }
//         }
//         else
//         {
//             // After an operand: expect infix operator, ), or end
//             let prec = infix_precedence(token.id);
//
//             if (prec > 0)
//             {
//                 // Infix binary operator
//                 let right_assoc = token_is_right_associative(token.id);
//                 sy->drain_operators(prec, right_assoc);
//                 sy->operators.push({
//                     .lexer_token_index = lexer_token_index,
//                     .precedence = prec,
//                     .arity = 2,
//                     .right_associative = right_assoc,
//                     .is_sentinel = false,
//                 });
//                 sy->advance();
//                 expect_operand = true;
//             }
//             else if (token.id == TokenId::RightParenthesis)
//             {
//                 // Drain to matching ( sentinel, wrap with ( )
//                 sy->drain_all();
//                 if (!sy->operators.is_empty() && sy->operators.top().is_sentinel)
//                 {
//                     let sentinel = sy->operators.pop();
//                     let inner = sy->operands.pop();
//                     let open_leaf = sy->rope.make_leaf(sentinel.lexer_token_index);
//                     let close_leaf = sy->rope.make_leaf(lexer_token_index);
//                     let wrapped = sy->rope.concat(open_leaf, sy->rope.concat(inner, close_leaf));
//                     sy->operands.push(wrapped);
//                     sy->advance();
//                     // After ), we still have a complete operand
//                 }
//                 else
//                 {
//                     // No matching ( — this ) belongs to an outer construct
//                     running = false;
//                 }
//             }
//             else
//             {
//                 // Not an infix operator — end of expression
//                 running = false;
//             }
//         }
//     }
//
//     sy->drain_all();
//
//     RopeRef result = no_rope;
//     if (!sy->operands.is_empty())
//     {
//         result = sy->operands.pop();
//     }
//     return result;
// }
//
// // ============================================================
// // Compound constructs — parsed as sub-invocations, producing
// // single rope segments that include delimiter tokens
// // ============================================================
//
// // Attribute list: [ expr, expr, ... ]
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_attribute_list(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // [
//     sy->advance();
//
//     while (!sy->at_end())
//     {
//         let token = sy->peek();
//
//         if (token.id == TokenId::RightBracket)
//         {
//             result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//             sy->advance();
//             break;
//         }
//
//         if (token.id == TokenId::Comma)
//         {
//             result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//             sy->advance();
//             continue;
//         }
//
//         LexerTokenIndex before = sy->index;
//         let expr = sy_parse_expression(sy);
//         if (expr.head != no_node)
//         {
//             result = sy->rope.concat(result, expr);
//         }
//         if (sy->index.v == before.v) { sy->advance(); }
//     }
//
//     return result;
// }
//
// // Argument/parameter list: ( name: type, ... )
// // Each parameter is parsed as an expression (: is infix, prec 2)
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_argument_list(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // (
//     sy->advance();
//
//     while (!sy->at_end())
//     {
//         let token = sy->peek();
//
//         if (token.id == TokenId::RightParenthesis)
//         {
//             result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//             sy->advance();
//             break;
//         }
//
//         if (token.id == TokenId::Comma)
//         {
//             result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//             sy->advance();
//             continue;
//         }
//
//         // Each param is an expression: name : type (: is infix prec 2)
//         LexerTokenIndex before = sy->index;
//         let param = sy_parse_expression(sy);
//         if (param.head != no_node)
//         {
//             result = sy->rope.concat(result, param);
//         }
//         if (sy->index.v == before.v) { sy->advance(); }
//     }
//
//     return result;
// }
//
// // Block: { statement statement ... }
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_block(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // {
//     sy->advance();
//
//     while (!sy->at_end())
//     {
//         let token = sy->peek();
//
//         if (token.id == TokenId::RightBrace)
//         {
//             result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//             sy->advance();
//             break;
//         }
//
//         LexerTokenIndex before = sy->index;
//         let stmt = sy_parse_statement(sy);
//         if (stmt.head != no_node)
//         {
//             result = sy->rope.concat(result, stmt);
//         }
//
//         if (sy->index.v == before.v)
//         {
//             sy->advance();
//         }
//     }
//
//     return result;
// }
//
// // for (binding_or_range) { body }
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_for(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // for
//     sy->advance();
//
//     let range = sy_parse_expression(sy);
//     if (range.head != no_node)
//     {
//         result = sy->rope.concat(result, range);
//     }
//
//     if (sy->peek().id == TokenId::LeftBrace)
//     {
//         result = sy->rope.concat(result, sy_parse_block(sy));
//     }
//
//     return result;
// }
//
// // while (condition) { body }
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_while(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // while
//     sy->advance();
//
//     let condition = sy_parse_expression(sy);
//     if (condition.head != no_node)
//     {
//         result = sy->rope.concat(result, condition);
//     }
//
//     if (sy->peek().id == TokenId::LeftBrace)
//     {
//         result = sy->rope.concat(result, sy_parse_block(sy));
//     }
//
//     return result;
// }
//
// // ============================================================
// // Keyword constructs — structurally parsed, building ropes
// // in prefix order. Expressions within are handled by the
// // shunting-yard.
// // ============================================================
//
// // fn [attrs] name [attrs] (params) ret_type { body }
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_function(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // fn
//     sy->advance();
//
//     // Optional first attribute list
//     if (sy->peek().id == TokenId::LeftBracket)
//     {
//         result = sy->rope.concat(result, sy_parse_attribute_list(sy));
//     }
//
//     // Function name
//     sy->peek(); // skip whitespace
//     result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
//     sy->advance();
//
//     // Optional second attribute list
//     if (sy->peek().id == TokenId::LeftBracket)
//     {
//         result = sy->rope.concat(result, sy_parse_attribute_list(sy));
//     }
//
//     // Argument list
//     if (sy->peek().id == TokenId::LeftParenthesis)
//     {
//         result = sy->rope.concat(result, sy_parse_argument_list(sy));
//     }
//
//     // Return type (parsed as expression)
//     let ret_type = sy_parse_expression(sy);
//     if (ret_type.head != no_node)
//     {
//         result = sy->rope.concat(result, ret_type);
//     }
//
//     // Body block
//     if (sy->peek().id == TokenId::LeftBrace)
//     {
//         result = sy->rope.concat(result, sy_parse_block(sy));
//     }
//
//     return result;
// }
//
// // if (condition) { then } [else { else }]
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_if(ShuntingYard* restrict sy)
// {
//     RopeRef result = sy->rope.make_leaf(sy->index); // if
//     sy->advance();
//
//     // Condition (parenthesized expression — SY handles the grouping)
//     let condition = sy_parse_expression(sy);
//     if (condition.head != no_node)
//     {
//         result = sy->rope.concat(result, condition);
//     }
//
//     // Then block
//     if (sy->peek().id == TokenId::LeftBrace)
//     {
//         result = sy->rope.concat(result, sy_parse_block(sy));
//     }
//
//     // Optional else
//     if (sy->peek().id == TokenId::Keyword_Else)
//     {
//         result = sy->rope.concat(result, sy->rope.make_leaf(sy->index)); // else
//         sy->advance();
//
//         if (sy->peek().id == TokenId::Keyword_If)
//         {
//             result = sy->rope.concat(result, sy_parse_if(sy));
//         }
//         else if (sy->peek().id == TokenId::LeftBrace)
//         {
//             result = sy->rope.concat(result, sy_parse_block(sy));
//         }
//     }
//
//     return result;
// }
//
// // ============================================================
// // Statement parsing
// // ============================================================
//
// BUSTER_GLOBAL_LOCAL RopeRef sy_parse_statement(ShuntingYard* restrict sy)
// {
//     RopeRef result = no_rope;
//     let token = sy->peek();
//
//     while (token.id == TokenId::Semicolon)
//     {
//         sy->error_count += 1;
//         sy->advance();
//         token = sy->peek();
//     }
//
//     switch (token.id)
//     {
//         break; case TokenId::Keyword_Function:
//         {
//             result = sy->rope.concat(result, sy_parse_function(sy));
//             sy_consume_disallowed_semicolons(sy);
//         }
//         break; case TokenId::Keyword_If:
//         {
//             result = sy->rope.concat(result, sy_parse_if(sy));
//             sy_consume_disallowed_semicolons(sy);
//         }
//         break; case TokenId::Keyword_For:
//         {
//             result = sy->rope.concat(result, sy_parse_for(sy));
//             sy_consume_disallowed_semicolons(sy);
//         }
//         break; case TokenId::Keyword_While:
//         {
//             result = sy->rope.concat(result, sy_parse_while(sy));
//             sy_consume_disallowed_semicolons(sy);
//         }
//         break; default:
//         {
//             // Expression statement: source syntax stays `expr;`, but the
//             // prefix reorder treats `;` as the unary statement operator.
//             let expr = sy_parse_expression(sy);
//             if (expr.head != no_node)
//             {
//                 if (sy->peek().id == TokenId::Semicolon)
//                 {
//                     result = sy->rope.combine_unary(sy->index, expr);
//                     sy->advance();
//                 }
//                 else
//                 {
//                     sy->error_count += 1;
//                 }
//             }
//         }
//     }
//
//     return result;
// }

ENUM_T(ParserDeclaration, u8,
        None,
        Function);

ENUM(FunctionState, u8,
    FunctionAttributeListParse,
    NameParse,
    SymbolAttributeListParse,
    ArgumentListParse,
    ReturnTypeParse);
STRUCT(ParserState)
{
    struct
    {
        FunctionState current_state;
        FLAG_ARRAY_U64(flags, FunctionState);
    } function;
    ParserDeclaration declaration_id;
};

STRUCT(ExtendedToken)
{
    u32 column:24;
    TokenId id;
    u32 length;
    u32 line;
    u32 offset;
};

static_assert(sizeof(ExtendedToken) == 16);

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 get_token_length(Token* restrict token,
        // These are pre-read
        TokenId token_id, u8 token_length)
{
    bool extended_length = token_length == 0;
    bool has_fake_length = token_id == TokenId::LineFeed || token_id == TokenId::SOF || token_id == TokenId::EOF;
    u32 length = has_fake_length ? 0 : (extended_length ? *(u32*)(token + 1) : token_length);
    return length;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 get_token_length(Token* restrict token)
{
    return get_token_length(token, token->id, token->length);
}

STRUCT(StateStack)
{
    Arena* arena;

    BUSTER_INLINE bool is_empty()
    {
        return arena->position < (arena_minimum_position + sizeof(ParserState));
    }

    BUSTER_INLINE ParserState* top()
    {
        return arena_get_pointer_at_position(arena, ParserState, arena->position - sizeof(ParserState));
    }

    BUSTER_INLINE ParserState pop()
    {
        BUSTER_CHECK(!is_empty());
        ParserState old_top = *top();
        arena->position -= sizeof(ParserState);
        return old_top;
    }

    BUSTER_INLINE ParserState* push()
    {
        let state = arena_allocate(arena, ParserState, 1);
        *state = {};
        return state;
    }

};

STRUCT(Parser)
{
    StateStack state_stack;
    Slice<Token> tokens;
    u32 token_index;
    const char8* source;
    u32 line_index;
    u32 line_offset;
    u32 column_index;
};

BUSTER_GLOBAL_LOCAL ExtendedToken get(Parser& parser)
{
    ExtendedToken result = {};
    bool is_noise = true;

    do
    {
        auto* restrict token = &parser.tokens[parser.token_index];
        let id = token->id;
        let token_length = token->length;
        result = {
            .id = id,
            .column = parser.column_index,
            .length = get_token_length(token),
            .line = parser.line_index,
            .offset = parser.line_offset + parser.column_index,
        };

        is_noise =
            id == TokenId::LineFeed ||
            id == TokenId::LineOffset ||
            id == TokenId::LineIndex ||
            id == TokenId::Tab ||
            id == TokenId::Space ||
            id == TokenId::Comment ||
            id == TokenId::CarriageReturn;

        // Line number and line offset are always 32-bit length
        let length = get_token_length(token, id, token_length);
        parser.line_index = id == TokenId::LineIndex ? length : parser.line_index;
        parser.line_offset = id == TokenId::LineOffset ? length : parser.line_offset;
        parser.column_index = parser.column_index + ((id == TokenId::LineIndex || id == TokenId::LineOffset) ? 0 : length);
        parser.token_index += 1 + ((u32)(token_length == 0) << 1);
    } while (is_noise);

    return result;
}

BUSTER_GLOBAL_LOCAL String8 get_string(Parser& parser, ExtendedToken token)
{
    String8 result = {};
    if (token.length)
    {
        result = { .pointer = (char8*)&parser.source[token.offset], .length = token.length };
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken expect(Parser& parser, TokenId id)
{
    let token = get(parser);

    if (token.id != id)
    {
        let string = get_string(parser, token);
        BUSTER_TRAP();
    }

    return token;
}

BUSTER_GLOBAL_LOCAL ParserState* state(Parser& parser)
{
    return parser.state_stack.top();
}

ENUM(IrFunctionAttribute,
    CallingConvention);

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

ENUM_T(IrSymbolAttribute, u8,
      Export);

BUSTER_GLOBAL_LOCAL String8 symbol_attribute_names[] = {
    [(u64)IrSymbolAttribute::Export] = S8("export"),
};

static_assert(BUSTER_ARRAY_LENGTH(symbol_attribute_names) == (u64)IrSymbolAttribute::Count);

BUSTER_GLOBAL_LOCAL void parse(const char8* restrict source, TokenizerResult tokenizer)
{
    Parser parser = {};
    parser.token_index += 1;
    parser.tokens = tokenizer.tokens;
    parser.source = source;
    parser.state_stack.arena = arena_create({});

    BUSTER_CHECK(parser.tokens[0].id == TokenId::SOF);

    // Push a dummy state so the stack is never empty
    parser.state_stack.push();

    while (true)
    {
        switch (state(parser)->declaration_id)
        {
            break; case ParserDeclaration::None:
            {
                auto token = get(parser);

                switch (token.id)
                {
                    break; case TokenId::Keyword_Function:
                    {
                        let function_state = parser.state_stack.push();
                        function_state->declaration_id = ParserDeclaration::Function;
                    }
                    break; default: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::Function:
            {
                auto token = get(parser);

                let has_parsed_return_type = flag_get(state(parser)->function.flags, FunctionState::ReturnTypeParse);
                let has_parsed_argument_list = flag_get(state(parser)->function.flags, FunctionState::ArgumentListParse);
                let has_parsed_symbol_attributes = flag_get(state(parser)->function.flags, FunctionState::SymbolAttributeListParse);
                let has_parsed_function_name = flag_get(state(parser)->function.flags, FunctionState::NameParse);
                let has_parsed_function_attributes = flag_get(state(parser)->function.flags, FunctionState::FunctionAttributeListParse);

                switch (token.id)
                {
                    break; case TokenId::LeftBracket:
                    {
                        bool error =
                            has_parsed_return_type ||
                            has_parsed_argument_list ||
                            has_parsed_symbol_attributes ||
                            (has_parsed_function_name && has_parsed_function_attributes && has_parsed_symbol_attributes);

                        if (error)
                        {
                            BUSTER_TRAP();
                        }

                        if (has_parsed_function_name)
                        {
                            if (has_parsed_symbol_attributes)
                            {
                                BUSTER_UNREACHABLE();
                            }

                            // parse symbol attributes

                            IrSymbolAttributes result = {};
                            state(parser)->function.current_state = FunctionState::SymbolAttributeListParse;

                            while (true)
                            {
                                let symbol_attribute_name_token = get(parser);
                                if (symbol_attribute_name_token.id == TokenId::RightBracket)
                                {
                                    break;
                                }

                                switch (symbol_attribute_name_token.id)
                                {
                                    break; case TokenId::Identifier:
                                    {
                                        let symbol_attribute_name = get_string(parser, symbol_attribute_name_token);

                                        let symbol_attribute = IrSymbolAttribute::Count;

                                        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(symbol_attribute_names); i += 1)
                                        {
                                            if (string8_equal(symbol_attribute_name, symbol_attribute_names[i]))
                                            {
                                                symbol_attribute = (IrSymbolAttribute)i;
                                                break;
                                            }
                                        }

                                        switch (symbol_attribute)
                                        {
                                            break; case IrSymbolAttribute::Export:
                                            {
                                                result.linkage = IrLinkage::External;
                                                result.exported = true;
                                            }
                                            break; break; case IrSymbolAttribute::Count: BUSTER_TRAP();
                                        }
                                    }
                                    break; default: BUSTER_TRAP();
                                }
                            }
                        }
                        else
                        {
                            if (has_parsed_function_attributes)
                            {
                                BUSTER_UNREACHABLE();
                            }

                            // parse function attributes
                            IrFunctionAttributes result = {};
                            state(parser)->function.current_state = FunctionState::FunctionAttributeListParse;

                            while (true)
                            {
                                let attribute_name_token = get(parser);
                                if (attribute_name_token.id == TokenId::RightBracket)
                                {
                                    break;
                                }

                                switch (attribute_name_token.id)
                                {
                                    break; case TokenId::Identifier:
                                    {
                                        let attribute_name_candidate = get_string(parser, attribute_name_token);

                                        let attribute = IrFunctionAttribute::Count;

                                        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(function_attribute_names); i += 1)
                                        {
                                            if (string8_equal(attribute_name_candidate, function_attribute_names[i]))
                                            {
                                                attribute = (IrFunctionAttribute)i;
                                                break;
                                            }
                                        }

                                        switch (attribute)
                                        {
                                            break; case IrFunctionAttribute::CallingConvention:
                                            {
                                                expect(parser, TokenId::LeftParenthesis);
                                                let calling_convention_name_candidate_token = expect(parser, TokenId::Identifier);
                                                expect(parser, TokenId::RightParenthesis);

                                                let calling_convention_name_candidate = get_string(parser, calling_convention_name_candidate_token);

                                                let calling_convention = IrCallingConvention::Count;

                                                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(calling_convention_names); i += 1)
                                                {
                                                    if (string8_equal(calling_convention_name_candidate, calling_convention_names[i]))
                                                    {
                                                        calling_convention = (IrCallingConvention)i;
                                                        break;
                                                    }
                                                }

                                                if (calling_convention == IrCallingConvention::Count)
                                                {
                                                    BUSTER_TRAP();
                                                }

                                                result.calling_convention = calling_convention;
                                            }
                                            break; case IrFunctionAttribute::Count: BUSTER_TRAP();
                                        }
                                    }
                                    break; default: BUSTER_TRAP();
                                }
                            }
                        }
                    }
                    break; case TokenId::Identifier:
                    {
                        if (!has_parsed_return_type && !has_parsed_argument_list && !has_parsed_symbol_attributes && !has_parsed_function_name)
                        {
                            state(parser)->function.current_state = FunctionState::NameParse;
                            let function_name = get_string(parser, token);
                        }
                        else
                        {
                            BUSTER_TRAP();
                        }
                    }
                    break; case TokenId::LeftParenthesis:
                    {
                        if (!has_parsed_function_name || has_parsed_return_type || has_parsed_argument_list)
                        {
                            BUSTER_TRAP();
                        }

                        state(parser)->function.current_state = FunctionState::ArgumentListParse;

                        while (true)
                        {
                            let argument_name_token = get(parser);
                            if (argument_name_token.id == TokenId::RightParenthesis)
                            {
                                break;
                            }

                            if (argument_name_token.id == TokenId::Identifier)
                            {
                                let argument_name = get_string(parser, argument_name_token);
                                expect(parser, TokenId::Colon);

                                BUSTER_TRAP();
                            }
                            else
                            {
                                BUSTER_TRAP();
                            }
                        }

                        BUSTER_TRAP();
                    }
                    break; default: BUSTER_UNREACHABLE();
                }

                flag_set(state(parser)->function.flags, state(parser)->function.current_state, true);
            }
            break; case ParserDeclaration::Count: BUSTER_UNREACHABLE();
        }
    }

    // return result;
}

// BUSTER_F_IMPL ParserResult parse(const char8* restrict source, TokenizerResult tokenizer)
// {
//     let top_level_declaration_arena = arena_create({});
//
//     ShuntingYard sy = {
//         .tokens = tokenizer.tokens.pointer,
//         .token_count = (u32)tokenizer.tokens.length,
//         .index = {},
//         .rope = { .arena = arena_create({}) },
//         .operators = { .arena = arena_create({}) },
//         .operands = { .arena = arena_create({}) },
//         .error_count = tokenizer.error_count,
//     };
//
//     // Skip SOF
//     if (sy.peek().id == TokenId::SOF)
//     {
//         sy.advance();
//     }
//
//     while (!sy.at_end() && sy.peek().id != TokenId::EOF)
//     {
//         LexerTokenIndex before = sy.index;
//         let stmt = sy_parse_statement(&sy);
//         if (stmt.head != no_node)
//         {
//             *arena_allocate(top_level_declaration_arena, RopeRef, 1) = stmt;
//         }
//
//         // Safety: if no progress was made, skip one token to avoid infinite loop
//         if (sy.index.v == before.v)
//         {
//             sy.advance();
//         }
//     }
//
//     ParserResult result = {};
//     result.lexer_tokens = tokenizer.tokens.pointer;
//     result.lexer_token_count = (u32)tokenizer.tokens.length;
//     result.error_count = sy.error_count;
//     result.source = source;
//     let top_level_declaration_count = (u32)(arena_buffer_size(top_level_declaration_arena) / sizeof(RopeRef));
//     result.top_level_declaration_count = top_level_declaration_count;
//     if (top_level_declaration_count)
//     {
//         let output_arena = arena_create({});
//         result = flatten_top_level_declarations(source, tokenizer, &sy.rope, {
//                 .pointer = (RopeRef*)arena_buffer_start(top_level_declaration_arena),
//                 .length = top_level_declaration_count,
//                 }, output_arena, sy.error_count);
//     }
//
//     return result;
// }

// ============================================================
// Debug: print token source text for each index in prefix order
// ============================================================

BUSTER_GLOBAL_LOCAL bool token_has_source_bytes(TokenId id);

BUSTER_F_IMPL u32 get_token_offset(Token* restrict tokens, u32 token_index)
{
    // Compute source byte offset by summing prior token lengths
    // Skip SOF (index 0) since it has no source bytes
    BUSTER_CHECK(tokens[0].id == TokenId::SOF);
    u32 offset = 0;
    for (u32 t = 1; t < token_index; )
    {
        let token = &tokens[t];
        u32 length = token->length;
        if (length == 0) { length = *(u32*)(&tokens[t + 1]); t += 3; }
        else { t += 1; }

        if (token_has_source_bytes(token->id))
        {
            offset += length;
        }
    }

    return offset;
}

BUSTER_GLOBAL_LOCAL bool token_has_source_bytes(TokenId id)
{
    bool result;
    switch (id)
    {
        break; case TokenId::SOF:
        case TokenId::EOF:
        case TokenId::LineOffset:
        case TokenId::LineIndex:
        {
            result = false;
        }
        break; default:
        {
            result = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL Token* get_token(Token* restrict tokens, u32 index)
{
    return &tokens[index];
}

BUSTER_F_IMPL u32 get_token_length(Token* restrict tokens, u32 lexer_token_index)
{
    return get_token_length(get_token(tokens, lexer_token_index));
}

BUSTER_GLOBAL_LOCAL String8 get_token_content_from_offset_and_length(const char8* source, u32 offset, u32 length)
{
    let text = (String8){ .pointer = (char8*)(source + offset), .length = length };
    return text;
}

// BUSTER_F_IMPL String8 get_token_content(const char8* source, Token* restrict tokens, u32 token_index)
// {
//     let offset = get_token_offset(tokens, token_index);
//     let length = get_token_length(tokens, token_index);
//     return get_token_content_from_offset_and_length(source, offset, length);
// }

// BUSTER_F_IMPL LineAndColumn get_token_line_and_column(const char* source, Token* restrict tokens, LexerTokenIndex lexer_token_index)
// {
//     BUSTER_TRAP();
// }
//
// BUSTER_GLOBAL_LOCAL void print_prefix(ParserResult parser_result, Token* tokens, const char8* source)
// {
//     for (u64 i = 0; i < parser_result.parser_token_count; i += 1)
//     {
//         let lexer_token_index = parser_result.parser_to_lexer_indices[i];
//
//         let offset = get_token_offset(tokens, lexer_token_index);
//         let length = get_token_length(tokens, lexer_token_index);
//
//         let id = tokens[lexer_token_index.v].id;
//         if (id == TokenId::SOF || id == TokenId::EOF)
//         {
//             string8_print(S8("[{u8}] "), (u8)id);
//         }
//         else if (token_has_source_bytes(id))
//         {
//             let text = get_token_content_from_offset_and_length(source, offset, length);
//             string8_print(S8("{S8} "), text);
//         }
//     }
//
//     string8_print(S8("\n"));
// }

// ============================================================
// Experiments
// ============================================================

BUSTER_GLOBAL_LOCAL void parse_experiment(Arena* arena, StringOs path)
{
    let position = arena->position;
    defer { arena->position = position; };

    let source = BYTE_SLICE_TO_STRING(8, file_read(arena, path, {}));

    let tokenizer = tokenize(arena, source.pointer, source.length);
    parse(source.pointer, tokenizer);

    // string8_print(S8("=== Input ===\n{S8}\n"), source);
    // string8_print(S8("=== Error Count ===\n{u64}\n"), result.error_count);
    // string8_print(S8("=== Prefix Output (token reordering) ===\n"));
    // print_prefix(result, tokenizer.tokens.pointer, source.pointer);
}

BUSTER_F_IMPL void parser_experiments()
{
    let arena = arena_create({});
    parse_experiment(arena, SOs("tests/basic.bbb"));
    parse_experiment(arena, SOs("tests/if_else.bbb"));
    parse_experiment(arena, SOs("tests/array_slices.bbb"));
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL String8 lexer_token_text(const char8* restrict source, Token* tokens, LexerTokenIndex lexer_token_index)
{
    String8 result = {};
    let token = get_token(tokens, lexer_token_index);
    if (token_has_source_bytes(token->id))
    {
        result = get_token_content(source, tokens, lexer_token_index);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 parser_prefix_text(Arena* arena, ParserResult parser_result)
{
    let parts = arena_allocate(arena, String8, parser_result.parser_token_count * 2);
    u64 part_count = 0;

    for (u32 i = 0; i < parser_result.parser_token_count; i += 1)
    {
        let lexer_token_index = parser_result.parser_to_lexer_indices[i];
        let text = lexer_token_text(parser_result.source, parser_result.lexer_tokens, lexer_token_index);
        if (text.length)
        {
            parts[part_count++] = text;
            if (i + 1 < parser_result.parser_token_count)
            {
                parts[part_count++] = S8(" ");
            }
        }
    }

    let result = string8_join_arena(arena, {
            .pointer = parts,
            .length = part_count,
            }, true);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 parser_top_level_declaration_prefix_text(Arena* arena, ParserResult parser_result, u32 declaration_index)
{
    let declaration = parser_result.top_level_declarations[declaration_index];
    let parser_token_count = declaration.end.v - declaration.start.v;
    let parts = arena_allocate(arena, String8, parser_token_count * 2);
    u64 part_count = 0;

    for (ParserTokenIndex i = declaration.start; i.v < declaration.end.v; i.v += 1)
    {
        let lexer_token_index = parser_result.parser_to_lexer_indices[i.v];
        let text = lexer_token_text(parser_result.source, parser_result.lexer_tokens, lexer_token_index);
        if (text.length)
        {
            parts[part_count++] = text;
            if (i.v + 1 < declaration.end.v)
            {
                parts[part_count++] = S8(" ");
            }
        }
    }

    let result = string8_join_arena(arena, {
            .pointer = parts,
            .length = part_count,
            }, true);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult parser_statement_semicolon_tests(UnitTestArguments* arguments)
{
    STRUCT(ParserStatementSemicolonTestCase)
    {
        String8 source;
        String8 expected_prefix;
        u64 expected_error_count;
    };

    BUSTER_GLOBAL_LOCAL ParserStatementSemicolonTestCase test_cases[] = {
        {
            .source = S8("a = 1;"),
            .expected_prefix = S8("; = a 1"),
            .expected_error_count = 0,
        },
        {
            .source = S8("return value;"),
            .expected_prefix = S8("; return value"),
            .expected_error_count = 0,
        },
    };

    UnitTestResult result = {};
    let arena = arguments->arena;

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_cases); i += 1)
    {
        let reset_position = arena->position;
        let test_case = test_cases[i];
        let tokenizer = tokenize(arena, test_case.source.pointer, test_case.source.length);
        let parser_result = parse(test_case.source.pointer, tokenizer);
        let prefix_text = parser_prefix_text(arena, parser_result);

        bool has_expected_prefix = string8_equal(prefix_text, test_case.expected_prefix);
        if (!has_expected_prefix)
        {
            buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected prefix {S8}, got {S8}"), test_case.expected_prefix, prefix_text);
        }
        result.succeeded_test_count += has_expected_prefix;
        result.test_count += 1;

        bool has_expected_error_count = parser_result.error_count == test_case.expected_error_count;
        if (!has_expected_error_count)
        {
            buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected error count {u64}, got {u64}"), test_case.expected_error_count, parser_result.error_count);
        }
        result.succeeded_test_count += has_expected_error_count;
        result.test_count += 1;

        arena->position = reset_position;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult parser_top_level_declaration_tests(UnitTestArguments* arguments)
{
    STRUCT(ParserTopLevelDeclarationTestCase)
    {
        String8 source;
        String8 expected_prefixes[2];
        u32 expected_parser_token_starts[2];
        u32 expected_parser_token_ends[2];
        u32 expected_top_level_declaration_count;
        u32 reserved;
    };

    BUSTER_GLOBAL_LOCAL ParserTopLevelDeclarationTestCase test_cases[] = {
        {
            .source = S8("a = 1; b = 2;"),
            .expected_prefixes = {
                S8("; = a 1"),
                S8("; = b 2"),
            },
            .expected_parser_token_starts = { 0, 4 },
            .expected_parser_token_ends = { 4, 8 },
            .expected_top_level_declaration_count = 2,
        },
        {
            .source = S8("fn [cc(c)] main() s32 { return 0; }"),
            .expected_prefixes = {
                S8("fn [ cc ( c ) ] main ( ) s32 { ; return 0 }"),
            },
            .expected_parser_token_starts = { 0 },
            .expected_parser_token_ends = { 16 },
            .expected_top_level_declaration_count = 1,
        },
    };

    UnitTestResult result = {};
    let arena = arguments->arena;

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_cases); i += 1)
    {
        let reset_position = arena->position;
        let test_case = test_cases[i];
        let tokenizer = tokenize(arena, test_case.source.pointer, test_case.source.length);
        let parser_result = parse(test_case.source.pointer, tokenizer);

        bool has_expected_declaration_count = parser_result.top_level_declaration_count == test_case.expected_top_level_declaration_count;
        if (!has_expected_declaration_count)
        {
            buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected top-level declaration count {u32}, got {u32}"), test_case.expected_top_level_declaration_count, parser_result.top_level_declaration_count);
        }
        result.succeeded_test_count += has_expected_declaration_count;
        result.test_count += 1;

        for (u32 declaration_index = 0; declaration_index < parser_result.top_level_declaration_count; declaration_index += 1)
        {
            let prefix_text = parser_top_level_declaration_prefix_text(arena, parser_result, declaration_index);

            bool has_expected_prefix = string8_equal(prefix_text, test_case.expected_prefixes[declaration_index]);
            if (!has_expected_prefix)
            {
                buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected declaration prefix {S8}, got {S8}"), test_case.expected_prefixes[declaration_index], prefix_text);
            }
            result.succeeded_test_count += has_expected_prefix;
            result.test_count += 1;

            let declaration = parser_result.top_level_declarations[declaration_index];
            bool has_expected_start = declaration.start.v == test_case.expected_parser_token_starts[declaration_index];
            if (!has_expected_start)
            {
                buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected parser token start {u32}, got {u32}"), test_case.expected_parser_token_starts[declaration_index], declaration.start);
            }
            result.succeeded_test_count += has_expected_start;
            result.test_count += 1;

            bool has_expected_end = declaration.end.v == test_case.expected_parser_token_ends[declaration_index];
            if (!has_expected_end)
            {
                buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected parser token end {u32}, got {u32}"), test_case.expected_parser_token_ends[declaration_index], declaration.end);
            }
            result.succeeded_test_count += has_expected_end;
            result.test_count += 1;
        }

        arena->position = reset_position;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult parser_multiline_prefix_tests(UnitTestArguments* arguments)
{
    STRUCT(ParserMultilinePrefixTestCase)
    {
        String8 source;
        String8 expected_prefix;
        u64 expected_error_count;
    };

    BUSTER_GLOBAL_LOCAL ParserMultilinePrefixTestCase test_cases[] = {
        {
            .source = S8("fn [cc(c)] main [export] (argument_count: u32, argv: &&u8, envp: &&u8) s32\n{\n    return 0;\n}"),
            .expected_prefix = S8("fn [ cc ( c ) ] main [ export ] ( : argument_count u32 , : argv & & u8 , : envp & & u8 ) s32 { ; return 0 }"),
            .expected_error_count = 0,
        },
    };

    UnitTestResult result = {};
    let arena = arguments->arena;

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_cases); i += 1)
    {
        let reset_position = arena->position;
        let test_case = test_cases[i];
        let tokenizer = tokenize(arena, test_case.source.pointer, test_case.source.length);
        let parser_result = parse(test_case.source.pointer, tokenizer);
        let prefix_text = parser_prefix_text(arena, parser_result);

        bool has_expected_prefix = string8_equal(prefix_text, test_case.expected_prefix);
        if (!has_expected_prefix)
        {
            buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected prefix {S8}, got {S8}"), test_case.expected_prefix, prefix_text);
        }
        result.succeeded_test_count += has_expected_prefix;
        result.test_count += 1;

        bool has_expected_error_count = parser_result.error_count == test_case.expected_error_count;
        if (!has_expected_error_count)
        {
            buster_test_error(__LINE__, S8(__FUNCTION__), S8(__FILE__), S8("expected error count {u64}, got {u64}"), test_case.expected_error_count, parser_result.error_count);
        }
        result.succeeded_test_count += has_expected_error_count;
        result.test_count += 1;

        arena->position = reset_position;
    }

    return result;
}

BUSTER_F_IMPL BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    consume_unit_tests(&result, parser_statement_semicolon_tests(arguments));
    consume_unit_tests(&result, parser_top_level_declaration_tests(arguments));
    consume_unit_tests(&result, parser_multiline_prefix_tests(arguments));
    return result;
}
#endif
