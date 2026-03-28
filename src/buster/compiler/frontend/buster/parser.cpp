#pragma once
#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/assertion.h>

ENUM_T(TokenId, u8,
    Error,
    Space,
    LineFeed,
    CarriageReturn,
    SOF,
    EOF,
    Identifier,
    Number,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    LeftParenthesis,
    RightParenthesis,
    ListStart,
    ListEnd,
    Equal,
    Greater,
    Less,
    Plus,
    Minus,
    Asterisk,
    Slash,
    Percentage,
    Colon,
    Semicolon,
    Comma,
    Ampersand,
    Keyword_First,
    Keyword_Return,
    Keyword_If,
    Keyword_Else,
    Keyword_Function,
    Keyword_Let,
    Keyword_Last,
    Nonsense);

BUSTER_GLOBAL_LOCAL constexpr u64 keyword_count = (u64)TokenId::Keyword_Last - (u64)TokenId::Keyword_First - 1;

BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
    [-1 + (u64)TokenId::Keyword_Function - (u64)TokenId::Keyword_First] = S8("fn"),
    [-1 + (u64)TokenId::Keyword_If - (u64)TokenId::Keyword_First] = S8("if"),
    [-1 + (u64)TokenId::Keyword_Else - (u64)TokenId::Keyword_First] = S8("else"),
    [-1 + (u64)TokenId::Keyword_Return - (u64)TokenId::Keyword_First] = S8("return"),
    [-1 + (u64)TokenId::Keyword_Let - (u64)TokenId::Keyword_First] = S8("let"),
};
static_assert(BUSTER_ARRAY_LENGTH(keyword_names) == keyword_count);

STRUCT(Token)
{
    TokenId id;
    u8 length;
};

static_assert(sizeof(Token) == 2);

STRUCT(TokenizerResult)
{
    Slice<Token> tokens;
    u64 error_count;
};

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

BUSTER_GLOBAL_LOCAL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length)
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

        while (true)
        {
            if (it >= top)
            {
                break;
            }

            let start = it;
            let ch = *start;

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
                break; case '+': { id = TokenId::Plus; it += 1; }
                break; case '-': { id = TokenId::Minus; it += 1; }
                break; case '*': { id = TokenId::Asterisk; it += 1; }
                break; case '/': { id = TokenId::Slash; it += 1; }
                break; case '=': { id = TokenId::Equal; it += 1; }
                break; case ':': { id = TokenId::Colon; it += 1; }
                break; case ';': { id = TokenId::Semicolon; it += 1; }
                break; case ',': { id = TokenId::Comma; it += 1; }
                break; case '&': { id = TokenId::Ampersand; it += 1; }
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

constexpr u32 no_node = UINT32_MAX;

STRUCT(RopeNode)
{
    u32 token_index;
    u32 next; // index of next RopeNode, no_node = end
};

STRUCT(RopeRef)
{
    u32 head;
    u32 tail;
};

constexpr RopeRef no_rope = { .head = no_node, .tail = no_node };

STRUCT(RopeArena)
{
    Arena* arena;

    BUSTER_INLINE u32 count()
    {
        return (u32)(arena_buffer_size(arena) / sizeof(RopeNode));
    }

    BUSTER_INLINE RopeNode* at(u32 index)
    {
        return arena_get_pointer(arena, RopeNode, arena_minimum_position + index * sizeof(RopeNode));
    }

    RopeRef make_leaf(u32 token_index)
    {
        u32 index = count();
        let node = arena_allocate(arena, RopeNode, 1);
        *node = { .token_index = token_index, .next = no_node };
        RopeRef result = { .head = index, .tail = index };
        return result;
    }

    // Concatenate: a then b
    RopeRef concat(RopeRef a, RopeRef b)
    {
        RopeRef result;
        if (a.head == no_node) { result = b; }
        else if (b.head == no_node) { result = a; }
        else
        {
            at(a.tail)->next = b.head;
            result = { .head = a.head, .tail = b.tail };
        }
        return result;
    }

    // Prefix-combine binary: op -> left -> right
    RopeRef combine_binary(u32 op_token_index, RopeRef left, RopeRef right)
    {
        let op = make_leaf(op_token_index);
        at(op.tail)->next = left.head;
        at(left.tail)->next = right.head;
        RopeRef result = { .head = op.head, .tail = right.tail };
        return result;
    }

    // Prefix-combine unary: op -> child
    RopeRef combine_unary(u32 op_token_index, RopeRef child)
    {
        let op = make_leaf(op_token_index);
        at(op.tail)->next = child.head;
        RopeRef result = { .head = op.head, .tail = child.tail };
        return result;
    }
};

// ============================================================
// Prefix output: flat array of token indices in preorder
// ============================================================

STRUCT(PrefixResult)
{
    u32* token_indices;
    u64 count;
};

BUSTER_GLOBAL_LOCAL PrefixResult flatten_rope(RopeArena* rope_arena, RopeRef ref, Arena* output)
{
    u32* start = (u32*)arena_current_pointer(output, alignof(u32));
    u32 count = 0;
    u32 current = ref.head;

    while (current != no_node)
    {
        let node = rope_arena->at(current);
        *arena_allocate(output, u32, 1) = node->token_index;
        count += 1;
        current = node->next;
    }

    PrefixResult result = {
        .token_indices = start,
        .count = count,
    };
    return result;
}

// ============================================================
// Operator precedence
// ============================================================

BUSTER_GLOBAL_LOCAL u8 infix_precedence(TokenId id)
{
    u8 result;
    switch (id)
    {
        break; case TokenId::Equal:      result = 1;
        break; case TokenId::Colon:      result = 2;
        break; case TokenId::Greater:    result = 3;
        break; case TokenId::Less:       result = 3;
        break; case TokenId::Plus:       result = 4;
        break; case TokenId::Minus:      result = 4;
        break; case TokenId::Asterisk:   result = 5;
        break; case TokenId::Slash:      result = 5;
        break; case TokenId::Percentage: result = 5;
        break; default: result = 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool token_is_right_associative(TokenId id)
{
    bool result;
    switch (id)
    {
        break; case TokenId::Equal: result = true;
        break; default: result = false;
    }
    return result;
}

// ============================================================
// Shunting-yard stacks
// ============================================================

STRUCT(OperatorEntry)
{
    u32 token_index;
    u8 precedence;
    u8 arity; // 1 = unary prefix, 2 = binary infix
    bool right_associative;
    bool is_sentinel; // for ( grouping
};

STRUCT(OperatorStack)
{
    Arena* arena;

    BUSTER_INLINE void push(OperatorEntry entry)
    {
        *arena_allocate(arena, OperatorEntry, 1) = entry;
    }

    BUSTER_INLINE bool is_empty()
    {
        return arena_buffer_is_empty(arena);
    }

    BUSTER_INLINE OperatorEntry top()
    {
        return *arena_get_pointer(arena, OperatorEntry, arena->position - sizeof(OperatorEntry));
    }

    BUSTER_INLINE OperatorEntry pop()
    {
        let position = arena->position - sizeof(OperatorEntry);
        let result = *arena_get_pointer(arena, OperatorEntry, position);
        arena->position = position;
        return result;
    }
};

STRUCT(OperandStack)
{
    Arena* arena;

    BUSTER_INLINE void push(RopeRef ref)
    {
        *arena_allocate(arena, RopeRef, 1) = ref;
    }

    BUSTER_INLINE bool is_empty()
    {
        return arena_buffer_is_empty(arena);
    }

    BUSTER_INLINE RopeRef pop()
    {
        let position = arena->position - sizeof(RopeRef);
        let result = *arena_get_pointer(arena, RopeRef, position);
        arena->position = position;
        return result;
    }
};

// ============================================================
// Shunting-yard parser state
// ============================================================

STRUCT(ShuntingYard)
{
    // Input
    Token* restrict tokens;
    const char8* restrict source;
    u32 token_count;
    u32 index;

    // Rope output
    RopeArena rope;

    // Shunting-yard stacks
    OperatorStack operators;
    OperandStack operands;

    BUSTER_INLINE bool at_end()
    {
        return index >= token_count;
    }

    // Skip whitespace, advancing index
    void skip_whitespace()
    {
        while (index < token_count)
        {
            let id = tokens[index].id;
            if (id == TokenId::Space || id == TokenId::LineFeed || id == TokenId::CarriageReturn)
            {
                advance();
            }
            else
            {
                break;
            }
        }
    }

    // Peek at next significant token
    Token peek()
    {
        skip_whitespace();
        Token result = {};
        if (index < token_count)
        {
            result = tokens[index];
        }
        return result;
    }

    // Advance past current token (handles big-length encoding)
    void advance()
    {
        if (index < token_count)
        {
            u32 length = tokens[index].length;
            if (length == 0)
            {
                index += 3;
            }
            else
            {
                index += 1;
            }
        }
    }

    // Pop top operator and combine with operands into a rope
    void combine_top()
    {
        let op = operators.pop();

        if (op.arity == 2)
        {
            let right = operands.pop();
            let left = operands.pop();
            operands.push(rope.combine_binary(op.token_index, left, right));
        }
        else if (op.arity == 1)
        {
            let child = operands.pop();
            operands.push(rope.combine_unary(op.token_index, child));
        }
    }

    // Drain operators with precedence >= threshold
    void drain_operators(u8 precedence, bool right_associative)
    {
        while (!operators.is_empty())
        {
            let top = operators.top();
            if (top.is_sentinel) { break; }

            bool should_pop = false;
            if (top.precedence > precedence)
            {
                should_pop = true;
            }
            else if (top.precedence == precedence && !right_associative)
            {
                should_pop = true;
            }

            if (!should_pop) { break; }
            combine_top();
        }
    }

    // Drain all non-sentinel operators
    void drain_all()
    {
        while (!operators.is_empty())
        {
            if (operators.top().is_sentinel) { break; }
            combine_top();
        }
    }
};

// Forward declarations
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_expression(ShuntingYard* restrict sy);
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_attribute_list(ShuntingYard* restrict sy);
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_argument_list(ShuntingYard* restrict sy);
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_block(ShuntingYard* restrict sy);
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_function(ShuntingYard* restrict sy);
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_if(ShuntingYard* restrict sy);
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_statement(ShuntingYard* restrict sy);

// ============================================================
// Expression parsing — core shunting-yard
//
// Operands (identifiers, numbers) pass through in order.
// Infix operators (+, -, *, /, %, >, <, =, :) are reordered
// via the operator stack so they appear before their operands
// in the output (prefix position).
// Unary prefix operators (&, -, return, let) are handled the
// same way with high precedence and arity 1.
// Parentheses act as grouping with sentinels on the op stack.
// ============================================================

BUSTER_GLOBAL_LOCAL RopeRef sy_parse_expression(ShuntingYard* restrict sy)
{
    bool expect_operand = true;
    bool running = true;

    while (running && !sy->at_end())
    {
        let token = sy->peek();
        let token_index = sy->index;

        if (expect_operand)
        {
            switch (token.id)
            {
                break; case TokenId::Identifier:
                {
                    sy->operands.push(sy->rope.make_leaf(token_index));
                    sy->advance();
                    expect_operand = false;
                }
                break; case TokenId::Number:
                {
                    sy->operands.push(sy->rope.make_leaf(token_index));
                    sy->advance();
                    expect_operand = false;
                }
                break; case TokenId::LeftParenthesis:
                {
                    // Grouping: push sentinel with ( token index
                    sy->operators.push({
                        .token_index = token_index,
                        .precedence = 0,
                        .arity = 0,
                        .right_associative = false,
                        .is_sentinel = true,
                    });
                    sy->advance();
                }
                break; case TokenId::Ampersand:
                {
                    // Unary prefix: address-of
                    sy->operators.push({
                        .token_index = token_index,
                        .precedence = 6,
                        .arity = 1,
                        .right_associative = true,
                        .is_sentinel = false,
                    });
                    sy->advance();
                }
                break; case TokenId::Minus:
                {
                    // Unary prefix: negation
                    sy->operators.push({
                        .token_index = token_index,
                        .precedence = 6,
                        .arity = 1,
                        .right_associative = true,
                        .is_sentinel = false,
                    });
                    sy->advance();
                }
                break; case TokenId::Keyword_Return:
                {
                    // Prefix unary keyword (very low precedence)
                    sy->operators.push({
                        .token_index = token_index,
                        .precedence = 0,
                        .arity = 1,
                        .right_associative = true,
                        .is_sentinel = false,
                    });
                    sy->advance();
                }
                break; case TokenId::Keyword_Let:
                {
                    // Prefix unary keyword (very low precedence)
                    sy->operators.push({
                        .token_index = token_index,
                        .precedence = 0,
                        .arity = 1,
                        .right_associative = true,
                        .is_sentinel = false,
                    });
                    sy->advance();
                }
                break; default:
                {
                    // Not a valid operand start — end of expression
                    running = false;
                }
            }
        }
        else
        {
            // After an operand: expect infix operator, ), or end
            let prec = infix_precedence(token.id);

            if (prec > 0)
            {
                // Infix binary operator
                let right_assoc = token_is_right_associative(token.id);
                sy->drain_operators(prec, right_assoc);
                sy->operators.push({
                    .token_index = token_index,
                    .precedence = prec,
                    .arity = 2,
                    .right_associative = right_assoc,
                    .is_sentinel = false,
                });
                sy->advance();
                expect_operand = true;
            }
            else if (token.id == TokenId::RightParenthesis)
            {
                // Drain to matching ( sentinel, wrap with ( )
                sy->drain_all();
                if (!sy->operators.is_empty() && sy->operators.top().is_sentinel)
                {
                    let sentinel = sy->operators.pop();
                    let inner = sy->operands.pop();
                    let open_leaf = sy->rope.make_leaf(sentinel.token_index);
                    let close_leaf = sy->rope.make_leaf(token_index);
                    let wrapped = sy->rope.concat(open_leaf, sy->rope.concat(inner, close_leaf));
                    sy->operands.push(wrapped);
                    sy->advance();
                    // After ), we still have a complete operand
                }
                else
                {
                    // No matching ( — this ) belongs to an outer construct
                    running = false;
                }
            }
            else
            {
                // Not an infix operator — end of expression
                running = false;
            }
        }
    }

    sy->drain_all();

    RopeRef result = no_rope;
    if (!sy->operands.is_empty())
    {
        result = sy->operands.pop();
    }
    return result;
}

// ============================================================
// Compound constructs — parsed as sub-invocations, producing
// single rope segments that include delimiter tokens
// ============================================================

// Attribute list: [ expr, expr, ... ]
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_attribute_list(ShuntingYard* restrict sy)
{
    RopeRef result = sy->rope.make_leaf(sy->index); // [
    sy->advance();

    while (!sy->at_end())
    {
        let token = sy->peek();

        if (token.id == TokenId::RightBracket)
        {
            result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
            sy->advance();
            break;
        }

        if (token.id == TokenId::Comma)
        {
            result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
            sy->advance();
            continue;
        }

        u32 before = sy->index;
        let expr = sy_parse_expression(sy);
        if (expr.head != no_node)
        {
            result = sy->rope.concat(result, expr);
        }
        if (sy->index == before) { sy->advance(); }
    }

    return result;
}

// Argument/parameter list: ( name: type, ... )
// Each parameter is parsed as an expression (: is infix, prec 2)
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_argument_list(ShuntingYard* restrict sy)
{
    RopeRef result = sy->rope.make_leaf(sy->index); // (
    sy->advance();

    while (!sy->at_end())
    {
        let token = sy->peek();

        if (token.id == TokenId::RightParenthesis)
        {
            result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
            sy->advance();
            break;
        }

        if (token.id == TokenId::Comma)
        {
            result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
            sy->advance();
            continue;
        }

        // Each param is an expression: name : type (: is infix prec 2)
        u32 before = sy->index;
        let param = sy_parse_expression(sy);
        if (param.head != no_node)
        {
            result = sy->rope.concat(result, param);
        }
        if (sy->index == before) { sy->advance(); }
    }

    return result;
}

// Block: { statement; statement; ... }
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_block(ShuntingYard* restrict sy)
{
    RopeRef result = sy->rope.make_leaf(sy->index); // {
    sy->advance();

    while (!sy->at_end())
    {
        let token = sy->peek();

        if (token.id == TokenId::RightBrace)
        {
            result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
            sy->advance();
            break;
        }

        u32 before = sy->index;
        let stmt = sy_parse_statement(sy);
        if (stmt.head != no_node)
        {
            result = sy->rope.concat(result, stmt);
        }

        if (sy->index == before)
        {
            sy->advance();
        }
    }

    return result;
}

// ============================================================
// Keyword constructs — structurally parsed, building ropes
// in prefix order. Expressions within are handled by the
// shunting-yard.
// ============================================================

// fn [attrs] name [attrs] (params) ret_type { body }
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_function(ShuntingYard* restrict sy)
{
    RopeRef result = sy->rope.make_leaf(sy->index); // fn
    sy->advance();

    // Optional first attribute list
    if (sy->peek().id == TokenId::LeftBracket)
    {
        result = sy->rope.concat(result, sy_parse_attribute_list(sy));
    }

    // Function name
    sy->peek(); // skip whitespace
    result = sy->rope.concat(result, sy->rope.make_leaf(sy->index));
    sy->advance();

    // Optional second attribute list
    if (sy->peek().id == TokenId::LeftBracket)
    {
        result = sy->rope.concat(result, sy_parse_attribute_list(sy));
    }

    // Argument list
    if (sy->peek().id == TokenId::LeftParenthesis)
    {
        result = sy->rope.concat(result, sy_parse_argument_list(sy));
    }

    // Return type (parsed as expression)
    let ret_type = sy_parse_expression(sy);
    if (ret_type.head != no_node)
    {
        result = sy->rope.concat(result, ret_type);
    }

    // Body block
    if (sy->peek().id == TokenId::LeftBrace)
    {
        result = sy->rope.concat(result, sy_parse_block(sy));
    }

    return result;
}

// if (condition) { then } [else { else }]
BUSTER_GLOBAL_LOCAL RopeRef sy_parse_if(ShuntingYard* restrict sy)
{
    RopeRef result = sy->rope.make_leaf(sy->index); // if
    sy->advance();

    // Condition (parenthesized expression — SY handles the grouping)
    let condition = sy_parse_expression(sy);
    if (condition.head != no_node)
    {
        result = sy->rope.concat(result, condition);
    }

    // Then block
    if (sy->peek().id == TokenId::LeftBrace)
    {
        result = sy->rope.concat(result, sy_parse_block(sy));
    }

    // Optional else
    if (sy->peek().id == TokenId::Keyword_Else)
    {
        result = sy->rope.concat(result, sy->rope.make_leaf(sy->index)); // else
        sy->advance();

        if (sy->peek().id == TokenId::Keyword_If)
        {
            result = sy->rope.concat(result, sy_parse_if(sy));
        }
        else if (sy->peek().id == TokenId::LeftBrace)
        {
            result = sy->rope.concat(result, sy_parse_block(sy));
        }
    }

    return result;
}

// ============================================================
// Statement parsing
// ============================================================

BUSTER_GLOBAL_LOCAL RopeRef sy_parse_statement(ShuntingYard* restrict sy)
{
    let token = sy->peek();
    RopeRef result = no_rope;

    if (token.id == TokenId::Semicolon)
    {
        result = sy->rope.make_leaf(sy->index); // ;
        sy->advance();

        let next = sy->peek();

        switch (next.id)
        {
            break; case TokenId::Keyword_Function:
            {
                result = sy->rope.concat(result, sy_parse_function(sy));
            }
            break; case TokenId::Keyword_If:
            {
                result = sy->rope.concat(result, sy_parse_if(sy));
            }
            break; default:
            {
                // Expression statement: let, return, assignment, etc.
                let expr = sy_parse_expression(sy);
                if (expr.head != no_node)
                {
                    result = sy->rope.concat(result, expr);
                }
            }
        }
    }

    return result;
}

// ============================================================
// Top-level parse
// ============================================================

BUSTER_GLOBAL_LOCAL PrefixResult parse(TokenizerResult tokenizer, const char8* source)
{
    ShuntingYard sy = {
        .tokens = tokenizer.tokens.pointer,
        .source = source,
        .token_count = (u32)tokenizer.tokens.length,
        .index = 0,
        .rope = { .arena = arena_create({}) },
        .operators = { .arena = arena_create({}) },
        .operands = { .arena = arena_create({}) },
    };

    // Skip SOF
    if (sy.peek().id == TokenId::SOF)
    {
        sy.advance();
    }

    RopeRef top = no_rope;

    while (!sy.at_end() && sy.peek().id != TokenId::EOF)
    {
        u32 before = sy.index;
        let stmt = sy_parse_statement(&sy);
        if (stmt.head != no_node)
        {
            top = sy.rope.concat(top, stmt);
        }

        // Safety: if no progress was made, skip one token to avoid infinite loop
        if (sy.index == before)
        {
            sy.advance();
        }
    }

    PrefixResult result = {};
    if (top.head != no_node)
    {
        let output_arena = arena_create({});
        result = flatten_rope(&sy.rope, top, output_arena);
    }
    return result;
}

// ============================================================
// Debug: print token source text for each index in prefix order
// ============================================================

BUSTER_GLOBAL_LOCAL void print_prefix(PrefixResult prefix, Token* tokens, const char8* source)
{
    for (u64 i = 0; i < prefix.count; i += 1)
    {
        u32 token_index = prefix.token_indices[i];

        // Compute source byte offset by summing prior token lengths
        // Skip SOF (index 0) since it has no source bytes
        u64 offset = 0;
        for (u32 t = 0; t < token_index; )
        {
            let id = tokens[t].id;
            u32 length = tokens[t].length;
            if (length == 0) { length = *(u32*)(&tokens[t + 1]); t += 3; }
            else { t += 1; }

            if (id != TokenId::SOF && id != TokenId::EOF)
            {
                offset += length;
            }
        }

        u32 length = tokens[token_index].length;
        if (length == 0) { length = *(u32*)(&tokens[token_index + 1]); }

        let id = tokens[token_index].id;
        if (id == TokenId::SOF || id == TokenId::EOF)
        {
            string8_print(S8("[{u8}] "), (u8)id);
        }
        else
        {
            let text = (String8){ .pointer = (char8*)(source + offset), .length = length };
            string8_print(S8("{S8} "), text);
        }
    }

    string8_print(S8("\n"));
}

// ============================================================
// Experiments
// ============================================================

BUSTER_F_DECL void parser_experiments()
{
    let arena = arena_create((ArenaCreation){});
    String8 source = S8(
            "; fn [cc] main [export] (argument_count: u32, argv: &&u8, envp: &&u8) s32\n"
            "{\n"
            "    ; let a: s32 = 0\n"
            "    ; let b: s32 = 0\n"
            "    ; if (a > 0)\n"
            "    {\n"
            "        ; a = b\n"
            "    }\n"
            "    else\n"
            "    {\n"
            "        ; b = a\n"
            "    }\n"
            "    ; return a + b\n"
            "}\n"
            );

    let tokenizer = tokenize(arena, source.pointer, source.length);
    let result = parse(tokenizer, source.pointer);

    string8_print(S8("=== Input ===\n{S8}\n"), source);
    string8_print(S8("=== Prefix Output (token reordering) ===\n"));
    print_prefix(result, tokenizer.tokens.pointer, source.pointer);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_IMPL BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    BUSTER_UNUSED(arguments);
    return result;
}
#endif
