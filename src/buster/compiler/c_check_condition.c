// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's diagnose_if and enable_if in C (#1447). Clang is the external oracle of the behavior; no clang code is
// incorporated. Each attribute's condition is read once, where its function is declared, in the function's parameter
// scope (check_input_condition_build): its names are looked up there (check_input_thread_name, as clang's parser keeps the
// prototype's scope open over the attributes after the declarator), and the declaration evaluator checks it and evaluates
// it. A condition that names none of the parameters is a constant then, or clang's "never produces a constant expression"
// error. One that names a parameter becomes a small program, its steps in postorder (CheckConditionStep): the largest
// subexpressions without a parameter are constants the evaluator gave, a sizeof's or alignof's too, and the others are
// the checker's own actions (check_action_scalar), which each call runs with its constant arguments bound to the
// parameters (c_check_condition_graph.c). An argument that isn't a constant leaves the condition unknown, as clang's
// EvaluateWithSubstitution leaves it. A condition whose shape the program doesn't take (a call, a subscript, an
// assignment) is "not yet supported" in user code; a system header's is kept and never decides anything, as #1361's
// fortify wrappers had them, and so are the object-size wrappers' (pass_object_size).
//
// Each function declaration where some condition holds (its own or one of an earlier redeclaration that it inherits) has
// its list, the conditions in the order clang reads them: the redeclarations' in source order (CheckInput.
// condition_declarations, by the declaration's name token, which is what a use's lookup finds). A use of the name reads
// that list. clang checks diagnose_if's conditions that name no parameter at each use of the name (DiagnoseUseOfDecl,
// check_input_condition_uses) and the others at each call (c_check_condition_graph.c); an error among those that hold is
// the only one given, the first, else every warning. An enable_if whose condition doesn't hold makes the call "no matching
// function", naming the first in the order clang checks them: the latest declaration's first, each declaration's in source
// order. A use of a function with an enable_if that isn't always true, other than a call's callee, is clang's "cannot take
// address" error. An overloadable function's conditions take part in its overload resolution (c_check_overload.c): an
// enable_if that doesn't admit a call makes its candidate not viable, more of them rank it better, and ones that differ
// make two overloads of one signature (check_condition_same, clang's profile of the expression); a call reads its
// selected candidate's list.
//
// A unit without these attributes pays a flag test (CheckInputReader.condition_attributes) where declarations are read
// and a count test where uses and calls are.

#define CHECK_CONDITION_OPAQUE_CALL 1u
#define CHECK_CONDITION_OPAQUE_WRITE 2u

// The unit's conditions as the checker reads them (c_check_condition_eval.c), kept in its graph for overload resolution.
static CheckConditionTable check_condition_table(const CheckInput* input)
{
    const CheckConditionTable table = {
        .conditions = input->conditions,
        .declarations = input->condition_declarations,
        .declaration_count = input->condition_declaration_count,
    };
    return table;
}

// The list of declaration `declaration` (CheckInput.condition_declarations, by binary search), or NULL.
static const CheckConditionDeclaration* check_condition_declaration(const CheckInput* input, uint32_t declaration)
{
    const CheckConditionTable table = check_condition_table(input);
    return check_condition_find(&table, declaration);
}

// The place among the function's parameters (from 0) of the parameter `token`'s name names, or BUSTER_CHECK_NONE: the
// parameters are `parameters`' list (CheckInputReader.parameter_next), the places counted by the top-level commas of the
// parameter list that `list` opens.
static uint32_t check_condition_parameter(const CheckInputReader* reader, uint32_t parameters, uint32_t list, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t declaration = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? reader->output->declarations[token] : BUSTER_CHECK_NONE;
    uint32_t found = BUSTER_CHECK_NONE;
    for (uint32_t named = parameters; found == BUSTER_CHECK_NONE && named != BUSTER_CHECK_NONE; named = check_sparse_get(reader->parameter_next, named))
    {
        found = named == declaration ? named : BUSTER_CHECK_NONE;
    }
    uint32_t place = found != BUSTER_CHECK_NONE && list < found ? 0 : BUSTER_CHECK_NONE;
    uint32_t depth = 0;
    for (uint32_t i = list + 1; place != BUSTER_CHECK_NONE && i < found; i += 1)
    {
        const uint8_t kind = parser->tokens.kinds[i];
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1u : 0u;
        depth -= (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) && depth != 0 ? 1u : 0u;
        place += depth == 0 && kind == PARSE_KIND_COMMA ? 1u : 0u;
    }
    return place;
}

// The first name in [first, end) of a variable other than a parameter: the subexpression clang's note of a condition that
// never produces a constant expression points at, or BUSTER_CHECK_NONE.
static uint32_t check_condition_variable(const CheckInputReader* reader, uint32_t parameters, uint32_t list, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t t = first; result == BUSTER_CHECK_NONE && t < end; t += 1)
    {
        const uint32_t declaration = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && parser->tree.roles[t] == PARSE_ROLE_OPERAND ? reader->output->declarations[t] :
                                                                                                                                       BUSTER_CHECK_NONE;
        const bool variable = declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_OBJECT &&
                              check_condition_parameter(reader, parameters, list, t) == BUSTER_CHECK_NONE;
        result = variable ? t : BUSTER_CHECK_NONE;
    }
    return result;
}

// clang's "'X' attribute expression never produces a constant expression" at the attribute, with its note at `note`, of
// kind `note_kind`.
static void check_condition_never_constant_note(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t note,
                                                CheckInputNoteKind note_kind)
{
    const uint32_t before = reader->output->report_count;
    check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_NEVER_CONSTANT, 0);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].note_token = note;
        reader->output->reports[before].note_kind = note != BUSTER_CHECK_NONE ? note_kind : CHECK_INPUT_NOTE_NONE;
    }
}

// The same, its note "subexpression not valid in a constant expression".
static void check_condition_never_constant(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t note)
{
    check_condition_never_constant_note(reader, attributes, attribute, note, CHECK_INPUT_NOTE_CONSTANT_SUBEXPRESSION);
}

// The first string token of an attribute argument [first, end), through the parentheses glibc writes around messages.
static uint32_t check_condition_string(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t string = first;
    uint32_t last = end;
    while (string + 1 < last && parser->tokens.kinds[string] == PARSE_KIND_L_PAREN && parser->tree.parents[string] == last - 1)
    {
        string += 1;
        last -= 1;
    }
    return string;
}

// Whether `token` is sizeof's, alignof's or typeof's keyword, whose operand is unevaluated.
static bool check_condition_unevaluated_keyword(const Parser* parser, uint32_t token)
{
    const uint32_t name = parser->tokens.names[token];
    return name != 0 && (name == C_NAME_SIZEOF || name == C_NAME_ALIGNOF || name == C_NAME_ALIGNOF_C11 || name == C_NAME_GNU_ALIGNOF || name == C_NAME_GNU_ALIGNOF2 ||
                         name == C_NAME_TYPEOF || name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF || name == C_NAME_GNU_TYPEOF2 ||
                         name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2);
}

// The CheckOp of an operator token with `operands` operands, or CHECK_OP_COUNT for one the steps don't take.
static CheckOp check_condition_op(uint8_t kind, uint32_t operands)
{
    CheckOp op = CHECK_OP_COUNT;
    if (operands == 1)
    {
        op = kind == PARSE_KIND_MINUS ? CHECK_OP_NEGATIVE : kind == PARSE_KIND_PLUS ? CHECK_OP_POSITIVE : kind == PARSE_KIND_EXCLAIM ? CHECK_OP_NOT :
             kind == PARSE_KIND_TILDE ? CHECK_OP_BIT_NOT : CHECK_OP_COUNT;
    }
    else if (operands == 2)
    {
        switch (kind)
        {
            case PARSE_KIND_STAR: op = CHECK_OP_MULTIPLY; break;
            case PARSE_KIND_SLASH: op = CHECK_OP_DIVIDE; break;
            case PARSE_KIND_PERCENT: op = CHECK_OP_REMAINDER; break;
            case PARSE_KIND_PLUS: op = CHECK_OP_ADD; break;
            case PARSE_KIND_MINUS: op = CHECK_OP_SUBTRACT; break;
            case PARSE_KIND_LESSLESS: op = CHECK_OP_SHIFT_LEFT; break;
            case PARSE_KIND_GREATERGREATER: op = CHECK_OP_SHIFT_RIGHT; break;
            case PARSE_KIND_LESS: op = CHECK_OP_LESS; break;
            case PARSE_KIND_LESSEQUAL: op = CHECK_OP_LESS_EQUAL; break;
            case PARSE_KIND_GREATER: op = CHECK_OP_GREATER; break;
            case PARSE_KIND_GREATEREQUAL: op = CHECK_OP_GREATER_EQUAL; break;
            case PARSE_KIND_EQUALEQUAL: op = CHECK_OP_EQUAL; break;
            case PARSE_KIND_EXCLAIMEQUAL: op = CHECK_OP_NOT_EQUAL; break;
            case PARSE_KIND_AMP: op = CHECK_OP_BIT_AND; break;
            case PARSE_KIND_PIPE: op = CHECK_OP_BIT_OR; break;
            case PARSE_KIND_CARET: op = CHECK_OP_BIT_XOR; break;
            case PARSE_KIND_AMPAMP: op = CHECK_OP_LOGICAL_AND; break;
            case PARSE_KIND_PIPEPIPE: op = CHECK_OP_LOGICAL_OR; break;
            default: break;
        }
    }
    return op;
}

// Whether some arguments skip the read at `token` of a condition [first, end): a `&&`'s or `||`'s right operand, or a
// conditional's second or third, holds it. A conditional's first operand comes before its `?`, a child of its `:`.
static bool check_condition_avoidable(const Parser* parser, uint32_t first, uint32_t end, uint32_t token)
{
    bool avoidable = false;
    for (uint32_t at = token; !avoidable && parser->tree.parents[at] >= first && parser->tree.parents[at] < end && parser->tree.parents[at] != at;
         at = parser->tree.parents[at])
    {
        const uint32_t above = parser->tree.parents[at];
        const uint8_t above_kind = parser->tokens.kinds[above];
        bool condition = false;
        for (uint32_t q = at + 1; above_kind == PARSE_KIND_COLON && !condition && q < above; q += 1)
        {
            condition = parser->tokens.kinds[q] == PARSE_KIND_QUESTION && parser->tree.parents[q] == above;
        }
        avoidable = ((above_kind == PARSE_KIND_AMPAMP || above_kind == PARSE_KIND_PIPEPIPE) && at > above) || (above_kind == PARSE_KIND_COLON && !condition);
    }
    return avoidable;
}

// Whether an operand of an unevaluated operator holds `token` of a condition [first, end): a sizeof's, an alignof's or a
// typeof's keyword is its node's child, or the child before a `)`'s opener.
static bool check_condition_unevaluated_operand(const Parser* parser, uint32_t first, uint32_t end, uint32_t token)
{
    bool unevaluated = false;
    for (uint32_t at = token; !unevaluated && parser->tree.parents[at] >= first && parser->tree.parents[at] < end && parser->tree.parents[at] != at;
         at = parser->tree.parents[at])
    {
        const uint32_t above = parser->tree.parents[at];
        unevaluated = check_condition_unevaluated_keyword(parser, above);
        for (uint32_t k = first; !unevaluated && k < above; k += 1)
        {
            unevaluated = parser->tree.parents[k] == above && check_condition_unevaluated_keyword(parser, k);
        }
    }
    return unevaluated;
}

// The first cast of a condition [first, end), by its `(`, that clang's constant evaluation refuses where no arguments skip
// it (#2253), or BUSTER_CHECK_NONE: a cast to void, its note "non-literal type 'void' cannot be used in a constant
// expression" (CHECK_INPUT_NOTE_CONSTANT_VOID), and one from an integer to a pointer but a null pointer constant's, or from
// a pointer to an integer but bool, "this conversion is not allowed in a constant expression"
// (CHECK_INPUT_NOTE_CONSTANT_CONVERSION), in `*note_kind`. Unevaluated operands' casts aren't read.
static uint32_t check_condition_cast(CheckInputReader* reader, uint32_t first, uint32_t end, CheckInputNoteKind* note_kind)
{
    const Parser* parser = reader->parser;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t t = first; result == BUSTER_CHECK_NONE && t < end; t += 1)
    {
        uint32_t opener = BUSTER_CHECK_NONE;
        const bool cast = parser->tokens.kinds[t] == PARSE_KIND_R_PAREN && parser->tree.roles[t] == PARSE_ROLE_CLOSE_CAST;
        for (uint32_t k = first; cast && opener == BUSTER_CHECK_NONE && k < t; k += 1)
        {
            opener = parser->tokens.kinds[k] == PARSE_KIND_L_PAREN && parser->tree.parents[k] == t ? k : BUSTER_CHECK_NONE;
        }
        const bool read = opener != BUSTER_CHECK_NONE && !check_condition_avoidable(parser, first, end, t) &&
                          !check_condition_unevaluated_operand(parser, first, end, t);
        const TypeId target = read ? type_strip_aligned(reader->types, check_input_constant_type(reader, opener + 1, t)) : 0;
        const uint8_t target_kind = target != 0 ? type_kind(reader->types, target) : BUSTER_TYPE_KIND_NONE;
        // The operand: the run of tokens past the `)` that the cast's node holds.
        uint32_t operand_end = t + 1;
        bool under = target_kind != BUSTER_TYPE_KIND_NONE && target_kind != BUSTER_TYPE_KIND_VOID;
        while (under && operand_end < end)
        {
            uint32_t at = operand_end;
            for (uint32_t steps = 0; at != t && at >= first && at < end && parser->tree.parents[at] != at && steps < end - first; steps += 1)
            {
                at = parser->tree.parents[at];
            }
            under = at == t;
            operand_end += under ? 1u : 0u;
        }
        CheckValue value = { 0 };
        TypeId unused = 0;
        const bool convert = (target_kind == BUSTER_TYPE_KIND_POINTER || ((target_kind == BUSTER_TYPE_KIND_INTEGER || target_kind == BUSTER_TYPE_KIND_BIT_INT ||
                             target_kind == BUSTER_TYPE_KIND_ENUM) && type_unqualified(target) != BUSTER_TYPE_BOOL)) && operand_end > t + 1 &&
                             check_input_constant_read(reader, t + 1, operand_end, false, false, true, &value, &unused, NULL, NULL);
        const uint8_t operand_kind = convert ? type_kind(reader->types, type_strip_aligned(reader->types, value.type)) : BUSTER_TYPE_KIND_NONE;
        const bool integer = operand_kind == BUSTER_TYPE_KIND_INTEGER || operand_kind == BUSTER_TYPE_KIND_BIT_INT || operand_kind == BUSTER_TYPE_KIND_ENUM;
        const bool pointer = operand_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(operand_kind) || operand_kind == BUSTER_TYPE_KIND_FUNCTION;
        const bool null = integer && value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == 0;
        const bool refused = target_kind == BUSTER_TYPE_KIND_VOID || (target_kind == BUSTER_TYPE_KIND_POINTER && integer && !null) ||
                             (target_kind != BUSTER_TYPE_KIND_POINTER && pointer);
        if (refused)
        {
            result = opener;
            *note_kind = target_kind == BUSTER_TYPE_KIND_VOID ? CHECK_INPUT_NOTE_CONSTANT_VOID : CHECK_INPUT_NOTE_CONSTANT_CONVERSION;
        }
    }
    return result;
}

typedef enum CheckConditionBuild
{
    CHECK_CONDITION_BUILT,
    CHECK_CONDITION_UNSUPPORTED,
    // An error was reported.
    CHECK_CONDITION_FAILED,
} CheckConditionBuild;

// A dependent condition [first, end)'s steps and constants, in the output arena (`condition`), from the parse tree: each
// node's children are the tokens whose parent it is, in source order, and its subtree is a run of tokens (c-parser.md).
// The postorder comes from an explicit stack. A subtree without a parameter, and a sizeof's or alignof's, is a constant
// the declaration evaluator gives (in the scope: check_input_constant_read's scoped names), or the condition never
// produces one.
static CheckConditionBuild check_condition_steps(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t parameters,
                                                 uint32_t list, TypeId function, uint32_t first, uint32_t end, CheckCondition* condition)
{
    const Parser* parser = reader->parser;
    const uint32_t n = end - first;
    const uint64_t mark = reader->scratch->length;
    uint32_t* first_child = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* last_child = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* next_sibling = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* stack = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* cursor = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* low = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* high = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint32_t* step_of = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n);
    uint8_t* named = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, n);
    uint8_t* constant_node = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, n);
    uint8_t* skipped = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, n);
    uint8_t* opaque = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, n);
    CheckConditionStep* steps = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckConditionStep, n);
    CheckValue* constants = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, n);
    const bool allocated = first_child != NULL && last_child != NULL && next_sibling != NULL && order != NULL && stack != NULL && cursor != NULL && low != NULL &&
                           high != NULL && step_of != NULL && named != NULL && constant_node != NULL && skipped != NULL && opaque != NULL && steps != NULL &&
                           constants != NULL;
    reader->output->resource |= !allocated;
    CheckConditionBuild result = allocated ? CHECK_CONDITION_BUILT : CHECK_CONDITION_FAILED;
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    for (uint32_t i = 0; result == CHECK_CONDITION_BUILT && i < n; i += 1)
    {
        first_child[i] = BUSTER_CHECK_NONE;
        last_child[i] = BUSTER_CHECK_NONE;
        next_sibling[i] = BUSTER_CHECK_NONE;
    }
    for (uint32_t i = 0; result == CHECK_CONDITION_BUILT && i < n; i += 1)
    {
        const uint32_t parent = parser->tree.parents[first + i];
        if (parent >= first && parent < end && parent != first + i)
        {
            const uint32_t p = parent - first;
            if (last_child[p] == BUSTER_CHECK_NONE)
            {
                first_child[p] = i;
            }
            else
            {
                next_sibling[last_child[p]] = i;
            }
            last_child[p] = i;
        }
        else
        {
            root = i;
            roots += 1;
        }
    }
    result = result == CHECK_CONDITION_BUILT && roots != 1 ? CHECK_CONDITION_UNSUPPORTED : result;
    // The postorder.
    uint32_t ordered = 0;
    uint32_t depth = 0;
    if (result == CHECK_CONDITION_BUILT)
    {
        stack[depth] = root;
        cursor[depth] = first_child[root];
        depth = 1;
    }
    while (result == CHECK_CONDITION_BUILT && depth != 0)
    {
        const uint32_t child = cursor[depth - 1];
        if (child != BUSTER_CHECK_NONE)
        {
            cursor[depth - 1] = next_sibling[child];
            stack[depth] = child;
            cursor[depth] = first_child[child];
            depth += 1;
        }
        else
        {
            depth -= 1;
            order[ordered++] = stack[depth];
        }
    }
    // Each node's run and whether it names a parameter.
    for (uint32_t k = 0; result == CHECK_CONDITION_BUILT && k < ordered; k += 1)
    {
        const uint32_t v = order[k];
        low[v] = v;
        high[v] = v;
        named[v] = check_condition_parameter(reader, parameters, list, first + v) != BUSTER_CHECK_NONE;
        for (uint32_t c = first_child[v]; c != BUSTER_CHECK_NONE; c = next_sibling[c])
        {
            low[v] = low[c] < low[v] ? low[c] : low[v];
            high[v] = high[c] > high[v] ? high[c] : high[v];
            named[v] |= named[c];
        }
        // A sizeof's or an alignof's operand isn't evaluated: its keyword's node, or the `)` whose run it begins, is a
        // constant, as a subtree without a parameter is.
        const uint32_t token = first + v;
        const bool unevaluated = (check_condition_unevaluated_keyword(parser, token) && first_child[v] != BUSTER_CHECK_NONE) ||
                                 (parser->tokens.kinds[token] == PARSE_KIND_R_PAREN && low[v] != v && check_condition_unevaluated_keyword(parser, first + low[v]));
        constant_node[v] = !named[v] || unevaluated;
        // What no constant evaluation gets past (#1447): a call of a declared function (CHECK_CONDITION_OPAQUE_CALL), and an
        // assignment or an increment (CHECK_CONDITION_OPAQUE_WRITE).
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t callee = first_child[v] != BUSTER_CHECK_NONE ? first + first_child[v] : BUSTER_CHECK_NONE;
        const uint32_t called = callee != BUSTER_CHECK_NONE && parser->tokens.kinds[callee] == PARSE_KIND_IDENTIFIER ? reader->output->declarations[callee] :
                                BUSTER_CHECK_NONE;
        const bool call = parser->tree.roles[token] == PARSE_ROLE_CLOSE_CALL && called < parser->tokens.count &&
                          reader->declaration_kinds[called] == C_BINDING_FUNCTION && first_child[callee - first] == BUSTER_CHECK_NONE;
        const bool write = parser->tree.roles[token] == PARSE_ROLE_ASSIGN || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS;
        opaque[v] = call ? CHECK_CONDITION_OPAQUE_CALL : write ? CHECK_CONDITION_OPAQUE_WRITE : 0;
    }
    // The nodes no step is made for, parents first: a constant's inside, a bracket's opener, a conditional's `?` and a
    // cast's type name.
    for (uint32_t k = ordered; result == CHECK_CONDITION_BUILT && k > 0; k -= 1)
    {
        const uint32_t v = order[k - 1];
        const uint32_t token = first + v;
        const uint32_t parent = parser->tree.parents[token];
        const bool top = !(parent >= first && parent < end && parent != token);
        const uint8_t kind = parser->tokens.kinds[token];
        const bool opener = ((kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE) && first_child[v] == BUSTER_CHECK_NONE) || kind == PARSE_KIND_QUESTION;
        const bool type_name = !top && parser->tokens.kinds[parent] == PARSE_KIND_R_PAREN && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_CAST && token < parent;
        const bool callee = !top && opaque[parent - first] == CHECK_CONDITION_OPAQUE_CALL && first_child[parent - first] == v;
        skipped[v] = !top && (skipped[parent - first] || constant_node[parent - first] || opener || type_name || callee);
    }
    uint32_t parameter_count = 0;
    const TypeId* parameter_types = type_parameters(reader->types, function, &parameter_count);
    uint32_t step_count = 0;
    uint32_t constant_count = 0;
    for (uint32_t k = 0; result == CHECK_CONDITION_BUILT && k < ordered; k += 1)
    {
        const uint32_t v = order[k];
        const uint32_t token = first + v;
        const uint8_t kind = parser->tokens.kinds[token];
        const uint8_t role = parser->tree.roles[token];
        // The operands: the children but the brackets' own openers and a conditional's `?`.
        uint32_t operands[4] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t operand_count = 0;
        uint32_t opener = BUSTER_CHECK_NONE;
        for (uint32_t c = first_child[v]; c != BUSTER_CHECK_NONE; c = next_sibling[c])
        {
            const uint8_t child_kind = parser->tokens.kinds[first + c];
            const bool open = (child_kind == PARSE_KIND_L_PAREN && first_child[c] == BUSTER_CHECK_NONE) || child_kind == PARSE_KIND_QUESTION;
            opener = open && child_kind == PARSE_KIND_L_PAREN ? c : opener;
            if (!open && operand_count < 4)
            {
                operands[operand_count] = c;
            }
            operand_count += open ? 0u : 1u;
        }
        if (!skipped[v] && constant_node[v])
        {
            CheckValue value = { 0 };
            TypeId unused = 0;
            const bool read = check_input_constant_read(reader, first + low[v], first + high[v] + 1, false, false, true, &value, &unused, NULL, NULL) &&
                              check_input_const_rvalue(reader, first + high[v] + 1, &value);
            const bool evaluated = read && value.constant_kind != CHECK_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE;
            // A variable's read never gives a constant. clang's check is whether some arguments give one: a read that a `&&`'s
            // or `||`'s right operand, or a conditional's second or third, holds can be skipped, and is an unknown value each
            // call's evaluation needs only where it reads it. Any other part the evaluator doesn't take alone (a call's
            // callee) isn't supported.
            const uint32_t variable = evaluated ? BUSTER_CHECK_NONE : check_condition_variable(reader, parameters, list, first + low[v], first + high[v] + 1);
            const bool avoidable = read && !evaluated && check_condition_avoidable(parser, first, end, token);
            // Or a call, an assignment or an increment in it, which no constant evaluation gets past: the first, the note's.
            uint32_t written = BUSTER_CHECK_NONE;
            for (uint32_t t = low[v]; !evaluated && written == BUSTER_CHECK_NONE && t <= high[v]; t += 1)
            {
                written = opaque[t] == CHECK_CONDITION_OPAQUE_CALL ? first + first_child[t] : opaque[t] != 0 ? first + t : BUSTER_CHECK_NONE;
            }
            const uint32_t noted = variable != BUSTER_CHECK_NONE && (written == BUSTER_CHECK_NONE || variable < written) ? variable : written;
            if (!evaluated && noted != BUSTER_CHECK_NONE && !avoidable)
            {
                check_condition_never_constant(reader, attributes, attribute, noted);
                result = CHECK_CONDITION_FAILED;
            }
            else if (!evaluated && !avoidable)
            {
                result = CHECK_CONDITION_UNSUPPORTED;
            }
            else
            {
                value.constant_kind = evaluated ? value.constant_kind : CHECK_CONSTANT_NONE;
                constants[constant_count] = value;
                steps[step_count] = (CheckConditionStep){ .op = BUSTER_CHECK_CONDITION_CONSTANT, .index = constant_count, .operands = { 0, 0, 0 } };
                constant_count += 1;
                step_of[v] = step_count++;
            }
        }
        else if (!skipped[v] && opaque[v] != 0)
        {
            // A call naming a parameter is unknown, as its arguments are; an assignment or an increment that can't be skipped
            // never gives a constant, at its operator, and one that can is unknown.
            const bool avoidable = opaque[v] == CHECK_CONDITION_OPAQUE_CALL || check_condition_avoidable(parser, first, end, token);
            if (!avoidable)
            {
                check_condition_never_constant(reader, attributes, attribute, token);
                result = CHECK_CONDITION_FAILED;
            }
            else
            {
                constants[constant_count] = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
                steps[step_count] = (CheckConditionStep){ .op = BUSTER_CHECK_CONDITION_CONSTANT, .index = constant_count, .operands = { 0, 0, 0 } };
                constant_count += 1;
                step_of[v] = step_count++;
            }
        }
        else if (!skipped[v] && kind == PARSE_KIND_COMMA && operand_count == 2 && opener == BUSTER_CHECK_NONE)
        {
            // A comma operator's value is its right operand's, where its left one's is known. A left operand cast to void,
            // which only a call's evaluation reaches (#2253: check_condition_cast refuses one no arguments skip), is
            // discarded.
            const uint32_t left = first + operands[0];
            uint32_t left_opener = BUSTER_CHECK_NONE;
            const bool left_cast = parser->tokens.kinds[left] == PARSE_KIND_R_PAREN && parser->tree.roles[left] == PARSE_ROLE_CLOSE_CAST;
            for (uint32_t c = left_cast ? first_child[operands[0]] : BUSTER_CHECK_NONE; c != BUSTER_CHECK_NONE && left_opener == BUSTER_CHECK_NONE; c = next_sibling[c])
            {
                left_opener = parser->tokens.kinds[first + c] == PARSE_KIND_L_PAREN ? first + c : BUSTER_CHECK_NONE;
            }
            const TypeId left_type = left_opener != BUSTER_CHECK_NONE ? check_input_constant_type(reader, left_opener + 1, left) : 0;
            if (left_type != 0 && type_kind(reader->types, type_strip_aligned(reader->types, left_type)) == BUSTER_TYPE_KIND_VOID)
            {
                step_of[v] = step_of[operands[1]];
            }
            else
            {
                steps[step_count] = (CheckConditionStep){ .op = BUSTER_CHECK_CONDITION_COMMA, .operands = { step_of[operands[0]], step_of[operands[1]], 0 } };
                step_of[v] = step_count++;
            }
        }
        else if (!skipped[v] && kind == PARSE_KIND_IDENTIFIER && first_child[v] == BUSTER_CHECK_NONE)
        {
            const uint32_t place = check_condition_parameter(reader, parameters, list, token);
            result = place < parameter_count ? result : CHECK_CONDITION_UNSUPPORTED;
            // A volatile parameter's read is never a constant (#2383): one that can't be skipped never gives a constant
            // expression, with clang's note at the name, and one that can is an unknown value that each call's evaluation
            // needs only where it reads it.
            const uint32_t declaration = reader->output->declarations[token];
            const bool qualified = declaration < parser->tokens.count && (type_qualifiers(reader->declared_types[declaration]) & BUSTER_TYPE_VOLATILE) != 0;
            const bool avoidable = qualified && check_condition_avoidable(parser, first, end, token);
            if (result == CHECK_CONDITION_BUILT && qualified && !avoidable)
            {
                check_condition_never_constant(reader, attributes, attribute, token);
                result = CHECK_CONDITION_FAILED;
            }
            else if (result == CHECK_CONDITION_BUILT && qualified)
            {
                constants[constant_count] = (CheckValue){ .type = type_unqualified(parameter_types[place]), .category = CHECK_RVALUE };
                steps[step_count] = (CheckConditionStep){ .op = BUSTER_CHECK_CONDITION_CONSTANT, .index = constant_count, .operands = { 0, 0, 0 } };
                constant_count += 1;
                step_of[v] = step_count++;
            }
            else if (result == CHECK_CONDITION_BUILT)
            {
                steps[step_count] = (CheckConditionStep){ .op = BUSTER_CHECK_CONDITION_PARAMETER, .index = place, .type = parameter_types[place] };
                step_of[v] = step_count++;
            }
        }
        else if (!skipped[v] && kind == PARSE_KIND_R_PAREN && role == PARSE_ROLE_CLOSE_GROUP && operand_count == 1)
        {
            step_of[v] = step_of[operands[0]];
        }
        else if (!skipped[v] && kind == PARSE_KIND_R_PAREN && role == PARSE_ROLE_CLOSE_CAST && opener != BUSTER_CHECK_NONE && operand_count != 0 &&
                 operands[operand_count < 4 ? operand_count - 1 : 3] > v)
        {
            const TypeId type = check_input_constant_type(reader, first + opener + 1, token);
            const uint32_t operand = operands[operand_count < 4 ? operand_count - 1 : 3];
            result = type != 0 && operand_count <= 4 && named[operand] ? result : CHECK_CONDITION_UNSUPPORTED;
            if (result == CHECK_CONDITION_BUILT)
            {
                steps[step_count] = (CheckConditionStep){ .op = CHECK_OP_CAST, .operands = { step_of[operand], 0, 0 }, .type = type };
                step_of[v] = step_count++;
            }
        }
        else if (!skipped[v] && kind == PARSE_KIND_COLON && operand_count == 3)
        {
            steps[step_count] = (CheckConditionStep){ .op = CHECK_OP_CONDITIONAL, .operands = { step_of[operands[0]], step_of[operands[1]], step_of[operands[2]] } };
            step_of[v] = step_count++;
        }
        else if (!skipped[v])
        {
            const CheckOp op = operand_count <= 2 && opener == BUSTER_CHECK_NONE ? check_condition_op(kind, operand_count) : CHECK_OP_COUNT;
            result = op != CHECK_OP_COUNT ? result : CHECK_CONDITION_UNSUPPORTED;
            if (result == CHECK_CONDITION_BUILT)
            {
                steps[step_count] = (CheckConditionStep){ .op = op, .operands = { step_of[operands[0]], operand_count == 2 ? step_of[operands[1]] : 0, 0 } };
                step_of[v] = step_count++;
            }
        }
    }
    if (result == CHECK_CONDITION_BUILT)
    {
        CheckConditionStep* kept_steps = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckConditionStep, step_count);
        CheckValue* kept_constants = constant_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckValue, constant_count) : NULL;
        reader->output->resource |= kept_steps == NULL || (constant_count != 0 && kept_constants == NULL);
        result = kept_steps != NULL && (constant_count == 0 || kept_constants != NULL) ? result : CHECK_CONDITION_FAILED;
        if (result == CHECK_CONDITION_BUILT)
        {
            memcpy(kept_steps, steps, (uint64_t)step_count * sizeof(CheckConditionStep));
            if (constant_count != 0)
            {
                memcpy(kept_constants, constants, (uint64_t)constant_count * sizeof(CheckValue));
            }
            condition->steps = kept_steps;
            condition->constants = kept_constants;
            condition->step_count = step_count;
        }
    }
    arena_pop_to(reader->scratch, mark);
    return result;
}

// One attribute's condition (above), into `condition`: its kind, message and state, its steps where it names a parameter.
// A system header's condition whose shape the steps don't take is opaque; a user's is "not yet supported".
static CheckConditionBuild check_condition_build(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t parameters,
                                                 uint32_t list, TypeId function, uint32_t name, bool system, CheckCondition* condition)
{
    const Parser* parser = reader->parser;
    uint32_t first = 0;
    uint32_t end = 0;
    uint32_t message_first = 0;
    uint32_t message_end = 0;
    bool valid = check_input_attribute_argument(attribute, 0, &first, &end) && check_input_attribute_argument(attribute, 1, &message_first, &message_end);
    *condition = (CheckCondition)
    {
        .attribute = attribute->first,
        .message = valid ? check_condition_string(parser, message_first, message_end) : BUSTER_CHECK_NONE,
        .declaration = name,
        .first = first,
        .end = end,
        .function = function,
        .profile = BUSTER_CHECK_NONE,
        .group = attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF ? attribute->warning_group : 0,
        .kind = CHECK_CONDITION_ENABLE,
        .state = CHECK_CONDITION_OPAQUE,
    };
    uint32_t type_first = 0;
    uint32_t type_end = 0;
    String8 type_text = { 0 };
    if (valid && attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF && check_input_attribute_argument(attribute, 2, &type_first, &type_end))
    {
        const uint32_t string = check_condition_string(parser, type_first, type_end);
        valid = check_input_attribute_string(reader, string, type_end - (string - type_first), false, &type_text);
        condition->kind = check_input_attribute_equal(type_text, "error") ? CHECK_CONDITION_ERROR : CHECK_CONDITION_WARNING;
    }
    // The names, a function parameter's first, and the place of the parameter each names.
    bool dependent = false;
    uint32_t* places = valid && end > first ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, end - first) : NULL;
    reader->output->resource |= valid && end > first && places == NULL;
    valid &= end <= first || places != NULL;
    for (uint32_t t = first; valid && t < end; t += 1)
    {
        const uint8_t previous = t > 0 ? parser->tokens.kinds[t - 1] : PARSE_KIND_END;
        places[t - first] = BUSTER_CHECK_NONE;
        if (parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && previous != PARSE_KIND_PERIOD && previous != PARSE_KIND_ARROW)
        {
            (void)check_input_thread_name(reader, parameters, NULL, 0, NULL, 0, t);
            places[t - first] = check_condition_parameter(reader, parameters, list, t);
            dependent |= places[t - first] != BUSTER_CHECK_NONE;
        }
    }
    condition->places = dependent ? places : NULL;
    CheckValue value = { 0 };
    bool call_failure = false;
    valid = valid && check_input_attribute_expression(reader, attributes, attribute, first, end, true, &value, &call_failure);
    CheckConditionBuild result = valid ? CHECK_CONDITION_BUILT : CHECK_CONDITION_FAILED;
    // A condition of type void isn't one (#2253), and a cast clang's constant evaluation refuses where no arguments skip it
    // never gives a constant (check_condition_cast).
    CheckInputNoteKind cast_kind = CHECK_INPUT_NOTE_NONE;
    const bool void_condition = valid && type_kind(reader->types, type_strip_aligned(reader->types, value.type)) == BUSTER_TYPE_KIND_VOID;
    const uint32_t cast = valid && !void_condition ? check_condition_cast(reader, first, end, &cast_kind) : BUSTER_CHECK_NONE;
    if (void_condition)
    {
        const uint32_t before = reader->output->report_count;
        check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_NOT_BOOL, 0);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].type = value.type;
        }
        result = CHECK_CONDITION_FAILED;
    }
    else if (cast != BUSTER_CHECK_NONE)
    {
        check_condition_never_constant_note(reader, attributes, attribute, cast, cast_kind);
        result = CHECK_CONDITION_FAILED;
    }
    else if (valid && !dependent)
    {
        bool truth = false;
        const bool known = value.constant_kind != CHECK_CONSTANT_NONE && check_input_const_rvalue(reader, end, &value) && check_action_truth(reader->types, &value, &truth);
        condition->state = truth ? CHECK_CONDITION_TRUE : CHECK_CONDITION_FALSE;
        if (!known)
        {
            // The note at the first variable it reads, or call of a declared function, assignment or increment it makes.
            const uint32_t variable = check_condition_variable(reader, parameters, list, first, end);
            uint32_t written = BUSTER_CHECK_NONE;
            for (uint32_t t = first; written == BUSTER_CHECK_NONE && t < end; t += 1)
            {
                const uint8_t kind = parser->tokens.kinds[t];
                const uint32_t called = kind == PARSE_KIND_IDENTIFIER && t + 1 < end && parser->tree.roles[t + 1] == PARSE_ROLE_OPEN_CALL ?
                                        reader->output->declarations[t] : BUSTER_CHECK_NONE;
                const bool call = called < parser->tokens.count && reader->declaration_kinds[called] == C_BINDING_FUNCTION;
                const bool write = parser->tree.roles[t] == PARSE_ROLE_ASSIGN || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS;
                written = call || write ? t : BUSTER_CHECK_NONE;
            }
            check_condition_never_constant(reader, attributes, attribute, variable != BUSTER_CHECK_NONE && variable < written ? variable : written);
            result = CHECK_CONDITION_FAILED;
        }
    }
    else if (valid)
    {
        result = check_action_is_scalar(reader->types, value.type) ? check_condition_steps(reader, attributes, attribute, parameters, list, function, first, end, condition) :
                                                                     CHECK_CONDITION_UNSUPPORTED;
        condition->state = result == CHECK_CONDITION_BUILT ? CHECK_CONDITION_DEPENDENT : CHECK_CONDITION_OPAQUE;
        result = result == CHECK_CONDITION_UNSUPPORTED && system ? CHECK_CONDITION_BUILT : result;
    }
    return result;
}

// Whether conditions `a` and `b` are the same expression as clang's profile of it says (StmtProfiler, canonical, which
// compareEnableIfAttrs compares): token for token, as the tree follows the tokens, a parameter's name by its place and
// type, a literal by its value and type (`0x1` is `1`, `'\1'` is `'\x01'`, a macro is what it expands to; `1L` isn't
// `1`), a string by its spelling, any other name by what it names and any other token by its kind. Parentheses are a
// tree's node, as clang's ParenExpr is: `(1)` isn't `1`.
static bool check_condition_same(CheckInputReader* reader, const CheckCondition* a, const CheckCondition* b)
{
    const Parser* parser = reader->parser;
    uint32_t a_count = 0;
    uint32_t b_count = 0;
    const TypeId* a_types = type_parameters(reader->types, a->function, &a_count);
    const TypeId* b_types = type_parameters(reader->types, b->function, &b_count);
    const uint32_t length = a->end - a->first;
    bool same = length == b->end - b->first;
    for (uint32_t k = 0; same && k < length; k += 1)
    {
        const uint32_t at = a->first + k;
        const uint32_t bt = b->first + k;
        const uint8_t kind = parser->tokens.kinds[at];
        same = kind == parser->tokens.kinds[bt];
        const uint32_t a_place = same && a->places != NULL ? a->places[k] : BUSTER_CHECK_NONE;
        const uint32_t b_place = same && b->places != NULL ? b->places[k] : BUSTER_CHECK_NONE;
        if (same && kind == PARSE_KIND_IDENTIFIER && (a_place != BUSTER_CHECK_NONE || b_place != BUSTER_CHECK_NONE))
        {
            const TypeAnswer typed = a_place < a_count && b_place < b_count ? type_same_canonical(reader->types, a_types[a_place], b_types[b_place]) : TYPE_ANSWER_NO;
            reader->output->resource |= typed == TYPE_ANSWER_FULL;
            same = a_place == b_place && typed == TYPE_ANSWER_YES;
        }
        else if (same && kind == PARSE_KIND_IDENTIFIER)
        {
            same = parser->tokens.names[at] == parser->tokens.names[bt] && reader->output->declarations[at] == reader->output->declarations[bt];
        }
        else if (same && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER))
        {
            CheckLiteral a_literal;
            CheckLiteral b_literal;
            check_input_literal(reader, at, &a_literal);
            check_input_literal(reader, bt, &b_literal);
            same = a_literal.valid && b_literal.valid && a_literal.type == b_literal.type && a_literal.bit_width == b_literal.bit_width &&
                   a_literal.integer == b_literal.integer && a_literal.real == b_literal.real &&
                   a_literal.floating_storage[0] == b_literal.floating_storage[0] && a_literal.floating_storage[1] == b_literal.floating_storage[1];
        }
        else if (same && kind != PARSE_KIND_STRING)
        {
            same = parser->tokens.names[at] == parser->tokens.names[bt];
        }
        else if (same)
        {
            const uint64_t mark = reader->scratch->length;
            const uint8_t* a_bytes = NULL;
            const uint8_t* b_bytes = NULL;
            uint32_t a_length = 0;
            uint32_t b_length = 0;
            same = check_input_spelling(reader, at, &a_bytes, &a_length) && check_input_spelling(reader, bt, &b_bytes, &b_length) && a_length == b_length &&
                   memcmp(a_bytes, b_bytes, a_length) == 0;
            arena_pop_to(reader->scratch, mark);
        }
    }
    return same;
}

// The profile of the unit's last condition, `index` (CheckCondition.profile): the first earlier enable_if condition that is
// the same expression (check_condition_same), else its own. Only enable_if's are compared (compareEnableIfAttrs).
static void check_condition_profile(CheckInputReader* reader, uint32_t index)
{
    CheckInput* input = reader->output;
    CheckCondition* condition = input->conditions + index;
    uint32_t profile = index;
    for (uint32_t i = 0; condition->kind == CHECK_CONDITION_ENABLE && condition->end > condition->first && profile == index && i < index; i += 1)
    {
        const CheckCondition* earlier = input->conditions + i;
        const bool candidate = earlier->kind == CHECK_CONDITION_ENABLE && earlier->profile == i && earlier->end - earlier->first == condition->end - condition->first &&
                               reader->parser->tokens.kinds[earlier->first] == reader->parser->tokens.kinds[condition->first];
        profile = candidate && check_condition_same(reader, earlier, condition) ? i : profile;
    }
    condition->profile = profile;
}

#include "c_check_condition_mangle.c"

// Appends `condition` to the unit's (CheckInput.conditions), giving its index, or BUSTER_CHECK_NONE where memory failed.
static uint32_t check_condition_append(CheckInputReader* reader, const CheckCondition* condition)
{
    CheckInput* input = reader->output;
    if (input->condition_count == input->condition_capacity)
    {
        const uint32_t capacity = input->condition_capacity != 0 ? input->condition_capacity * 2 : 8;
        CheckCondition* grown = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckCondition, capacity);
        if (grown != NULL && input->condition_count != 0)
        {
            memcpy(grown, input->conditions, (uint64_t)input->condition_count * sizeof(CheckCondition));
        }
        input->resource |= grown == NULL;
        input->conditions = grown != NULL ? grown : input->conditions;
        input->condition_capacity = grown != NULL ? capacity : input->condition_capacity;
    }
    const bool room = input->condition_count < input->condition_capacity;
    if (room)
    {
        input->conditions[input->condition_count] = *condition;
    }
    return room ? input->condition_count++ : BUSTER_CHECK_NONE;
}

// The previous declaration of overloadable function `token` of type `type`, whose own conditions are `own`'s, or
// BUSTER_CHECK_NONE: the latest declaration of its signature in the unit (check_input_overload_prior's walk) whose
// enable_ifs are the same expressions in the same order (check_condition_compare_enable), as clang's IsOverload compares
// them.
static uint32_t check_condition_overload_prior(CheckInputReader* reader, uint32_t token, TypeId type, const CheckConditionDeclaration* own)
{
    const uint32_t name = reader->parser->tokens.names[token];
    const CheckInputAttributes* attributes = check_input_attributes_at(reader->output, token);
    const bool object_size = attributes != NULL && attributes->object_sizes != NULL;
    uint64_t signature_hash = 0;
    const bool hashed = reader->overload_candidates != NULL &&
                        check_overload_signature_hash(&reader->overload_index, reader->types, type, reader->scratch, &signature_hash);
    signature_hash = object_size ? type_hash_mix(signature_hash, 1) : signature_hash;
    reader->output->resource |= reader->overload_candidates != NULL && !hashed;
    const uint64_t key = check_overload_index_key(signature_hash, name, UINT32_MAX);
    const uint32_t slot = hashed ? check_overload_index_find(&reader->overload_index, key, name, UINT32_MAX, reader->parser->options.scalar) : UINT32_MAX;
    const CheckConditionTable table = check_condition_table(reader->output);
    uint32_t prior = BUSTER_CHECK_NONE;
    for (uint32_t linked = hashed ? check_overload_index_head(&reader->overload_index, slot) : UINT32_MAX; prior == BUSTER_CHECK_NONE && linked != UINT32_MAX;
         linked = reader->overload_index.linked_next[linked])
    {
        const CheckOverloadCandidate* candidate = reader->overload_candidates + linked;
        const TypeAnswer signature = (candidate->object_sizes != NULL) == object_size ? check_overload_signature(reader->types, candidate->type, type) : TYPE_ANSWER_NO;
        reader->output->resource |= signature == TYPE_ANSWER_FULL;
        const bool same = signature == TYPE_ANSWER_YES && check_condition_compare_enable(&table, check_condition_find(&table, candidate->declaration), own) == 0;
        prior = same ? candidate->declaration : BUSTER_CHECK_NONE;
    }
    return prior;
}

// The conditions of function declaration `name` (above): its own attributes' (`attributes`, null for none) after those
// its previous declaration `previous` has, the list kept where any holds. The function's type is `type`, its parameters'
// names `parameters` and its parameter list's `(` `list`. False after an error.
static bool check_input_conditions(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t name, TypeId type, uint32_t parameters, uint32_t list,
                                   uint32_t previous)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const TypeId function = type_strip_aligned(reader->types, type);
    const bool overloadable = (attributes != NULL && (check_input_overload_flags(attributes) & BUSTER_CHECK_OVERLOADABLE) != 0) ||
                              (reader->overload_candidates != NULL && reader->overload_index.active[parser->tokens.names[name]]);
    uint32_t own = 0;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        own += (attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF || attribute->kind == CHECK_ATTRIBUTE_ENABLE_IF) && !attribute->ignored && !attribute->subject_ignored ? 1u : 0u;
    }
    // #1361's object-size wrappers in a system header keep their conditions unevaluated, as before; a user's are read as
    // any other function's (#2147), but a shape the steps don't take is kept and decides nothing.
    const bool system = check_graph_system_token(&input->graph, name);
    const bool object_sized = attributes != NULL && attributes->object_sizes != NULL;
    const bool sized = object_sized && system;
    // Its own conditions first, in source order.
    uint32_t* own_indices = own != 0 && !sized ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, own) : NULL;
    input->resource |= own != 0 && !sized && own_indices == NULL;
    bool valid = !input->resource;
    uint32_t own_count = 0;
    for (uint32_t i = 0; valid && own_indices != NULL && attributes != NULL && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        if ((attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF || attribute->kind == CHECK_ATTRIBUTE_ENABLE_IF) && !attribute->ignored && !attribute->subject_ignored)
        {
            CheckCondition condition;
            const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, attribute);
            // (A user's object-size wrapper keeps a shape the steps don't take, a `__builtin_object_size` of its parameter, as
            // a system header's condition is kept: it decides nothing.)
            const CheckConditionBuild built = check_condition_build(reader, attributes, attribute, parameters, list, function, name, system || object_sized,
                                                                    &condition);
            // A system header's condition whose shape the steps don't take is kept and decides nothing.
            if (built == CHECK_CONDITION_UNSUPPORTED)
            {
                check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            }
            // A condition with an error of its own is dropped, as clang drops the attribute, and the unit goes on (#2383):
            // its error is recovered (phase 1 of semantic error recovery), completing at the declarator's end, after the
            // warnings its expression gave.
            const bool dropped = built == CHECK_CONDITION_FAILED && check_input_attribute_errors_recover(reader, attributes, &recovery, reader->attribute_completion);
            attribute->ignored |= dropped;
            valid = built == CHECK_CONDITION_BUILT || dropped;
            const uint32_t index = valid && !dropped ? check_condition_append(reader, &condition) : BUSTER_CHECK_NONE;
            valid &= dropped || index != BUSTER_CHECK_NONE;
            if (valid && !dropped)
            {
                check_condition_profile(reader, index);
                if (overloadable && condition.kind == CHECK_CONDITION_ENABLE)
                {
                    const String8 mangled = check_condition_mangle(reader, input->conditions + index);
                    input->conditions[index].mangled = mangled.pointer;
                    input->conditions[index].mangled_length = (uint32_t)mangled.length;
                }
                own_indices[own_count++] = index;
            }
        }
    }
    // Then the ones of the previous declaration it inherits. An overloadable function's is the latest of its signature and
    // its enable_ifs (check_condition_overload_prior): clang's IsOverload makes a declaration whose enable_ifs differ another
    // overload, and a redeclaration's are its own again, so only the previous one's others are inherited.
    const CheckConditionDeclaration own_entry = { .declaration = name, .count = own_count, .conditions = own_indices };
    previous = valid && overloadable && !sized ? check_condition_overload_prior(reader, name, type, &own_entry) : previous;
    const CheckConditionDeclaration* inherited = valid && !sized && previous < parser->tokens.count && input->condition_declaration_count != 0 ?
                                                 check_condition_declaration(input, previous) : NULL;
    uint32_t inherited_count = 0;
    for (uint32_t i = 0; inherited != NULL && i < inherited->count; i += 1)
    {
        inherited_count += !overloadable || input->conditions[inherited->conditions[i]].kind != CHECK_CONDITION_ENABLE ? 1u : 0u;
    }
    uint32_t* list_indices = inherited_count == 0 ? own_indices :
                             valid ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, (uint64_t)inherited_count + own_count) : NULL;
    input->resource |= valid && inherited_count != 0 && list_indices == NULL;
    valid &= !input->resource;
    uint32_t count = inherited_count == 0 ? own_count : 0;
    for (uint32_t i = 0; valid && inherited_count != 0 && i < inherited->count; i += 1)
    {
        list_indices[count] = inherited->conditions[i];
        count += !overloadable || input->conditions[inherited->conditions[i]].kind != CHECK_CONDITION_ENABLE ? 1u : 0u;
    }
    for (uint32_t i = 0; valid && inherited_count != 0 && i < own_count; i += 1)
    {
        list_indices[count++] = own_indices[i];
    }
    if (valid && count != 0)
    {
        CheckConditionDeclaration entry = { .declaration = name, .count = count, .conditions = list_indices };
        for (uint32_t i = 0; i < count; i += 1)
        {
            const CheckCondition* condition = input->conditions + list_indices[i];
            entry.independent |= condition->kind != CHECK_CONDITION_ENABLE && condition->state == CHECK_CONDITION_TRUE;
            entry.restricted |= condition->kind == CHECK_CONDITION_ENABLE && condition->state != CHECK_CONDITION_TRUE && condition->state != CHECK_CONDITION_OPAQUE;
            entry.dependent |= condition->state == CHECK_CONDITION_DEPENDENT || (condition->kind == CHECK_CONDITION_ENABLE && condition->state == CHECK_CONDITION_FALSE);
        }
        if (input->condition_declaration_count == input->condition_declaration_capacity)
        {
            const uint32_t capacity = input->condition_declaration_capacity != 0 ? input->condition_declaration_capacity * 2 : 8;
            CheckConditionDeclaration* grown = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckConditionDeclaration, capacity);
            if (grown != NULL && input->condition_declaration_count != 0)
            {
                memcpy(grown, input->condition_declarations, (uint64_t)input->condition_declaration_count * sizeof(CheckConditionDeclaration));
            }
            input->resource |= grown == NULL;
            input->condition_declarations = grown != NULL ? grown : input->condition_declarations;
            input->condition_declaration_capacity = grown != NULL ? capacity : input->condition_declaration_capacity;
        }
        valid = input->condition_declaration_count < input->condition_declaration_capacity;
        // In ascending order of the declarations' tokens: source order appends, and a declaration read out of it (a
        // deferred body's) is moved into place.
        uint32_t at = valid ? input->condition_declaration_count : 0;
        while (valid && at > 0 && input->condition_declarations[at - 1].declaration > name)
        {
            input->condition_declarations[at] = input->condition_declarations[at - 1];
            at -= 1;
        }
        if (valid && at > 0 && input->condition_declarations[at - 1].declaration == name)
        {
            // The same declaration read again: its list is replaced.
            memmove(input->condition_declarations + at, input->condition_declarations + at + 1,
                    (uint64_t)(input->condition_declaration_count - at) * sizeof(CheckConditionDeclaration));
            at -= 1;
            input->condition_declaration_count -= 1;
        }
        if (valid)
        {
            input->condition_declarations[at] = entry;
            input->condition_declaration_count += 1;
        }
    }
    input->graph.conditions = check_condition_table(input);
    return valid;
}

// Whether the name at `token` is a call's callee, through the parentheses around it.
static bool check_condition_callee(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t before = token;
    uint32_t after = token;
    while (before > 0 && after + 1 < count && parser->tokens.kinds[before - 1] == PARSE_KIND_L_PAREN && parser->tree.parents[before - 1] == after + 1)
    {
        before -= 1;
        after += 1;
    }
    return after + 1 < count && parser->tokens.kinds[after + 1] == PARSE_KIND_L_PAREN && parser->tree.roles[after + 1] == PARSE_ROLE_OPEN_CALL;
}

// Whether the name at `token` is a sizeof's, an alignof's or a typeof's operand, through the parentheses around it, which
// clang doesn't convert to an address (inside an operation there, as `sizeof(q == r)`, it does).
static bool check_condition_unevaluated(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t before = token;
    uint32_t after = token;
    while (before > 0 && after + 1 < count && parser->tokens.kinds[before - 1] == PARSE_KIND_L_PAREN && parser->tree.parents[before - 1] == after + 1)
    {
        before -= 1;
        after += 1;
    }
    return before > 0 && check_condition_unevaluated_keyword(parser, before - 1);
}

// diagnose_if's diagnostics of `entry`'s conditions that `holds` says hold (its ith, the list's ith), at `location`: the
// first error of no warning group alone, else each warning, each with its note at its attribute. One of a warning group
// is clang's custom diagnostic of that group, a warning or an error by default, without the note (payload.data: the
// group's index plus one, BUSTER_CHECK_WARNING_GROUP_ERROR for an error's).
static void check_condition_diagnose(CheckInputReader* reader, const CheckConditionDeclaration* entry, uint32_t location, const bool* holds)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    uint32_t error = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; error == BUSTER_CHECK_NONE && i < entry->count; i += 1)
    {
        const CheckCondition* held = input->conditions + entry->conditions[i];
        error = holds[i] && held->kind == CHECK_CONDITION_ERROR && held->group == 0 ? entry->conditions[i] : BUSTER_CHECK_NONE;
    }
    if (error != BUSTER_CHECK_NONE)
    {
        const CheckCondition* condition = input->conditions + error;
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, location, CHECK_INPUT_DIAGNOSE_IF_ERROR);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].note_token = condition->attribute;
            reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_NONE;
            reader->output->reports[before].detail = condition->message + 1;
            reader->output->reports[before].value = condition->declaration;
        }
    }
    for (uint32_t i = 0; error == BUSTER_CHECK_NONE && i < entry->count; i += 1)
    {
        const CheckCondition* condition = input->conditions + entry->conditions[i];
        if (holds[i] && (condition->kind == CHECK_CONDITION_WARNING || (condition->kind == CHECK_CONDITION_ERROR && condition->group != 0)))
        {
            const uint32_t group = condition->group != 0 ? condition->group | (condition->kind == CHECK_CONDITION_ERROR ? BUSTER_CHECK_WARNING_GROUP_ERROR : 0u) : 0u;
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_USER_DEFINED_WARNINGS,
                .kind = CHECK_WARNING_EVENT_DIAGNOSE_IF,
                .token = location,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = location + 1,
                .payload = { .name = parser->tokens.names[condition->declaration], .token = condition->message, .data = group },
            };
            const CheckWarningNote note =
            {
                .kind = CHECK_WARNING_NOTE_DIAGNOSE_IF,
                .token = condition->attribute,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .name = parser->tokens.names[condition->declaration], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_event(reader, &event, &note, condition->group == 0 ? 1u : 0u);
        }
    }
}

// One use of a function with conditions, at `token`, of `entry`'s declaration: the diagnose_if conditions that name no
// parameter and hold. Whether the use converts the function's name to its address where an enable_if restricts it (other
// than a call's callee and outside sizeof's, alignof's and typeof's operands), whose error check_condition_addresses decides.
static bool check_condition_use(CheckInputReader* reader, uint32_t token, const CheckConditionDeclaration* entry)
{
    const CheckInput* input = reader->output;
    const uint64_t mark = reader->scratch->length;
    bool* holds = entry->independent ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, bool, entry->count) : NULL;
    reader->output->resource |= entry->independent && holds == NULL;
    for (uint32_t i = 0; holds != NULL && i < entry->count; i += 1)
    {
        const CheckCondition* condition = input->conditions + entry->conditions[i];
        holds[i] = condition->kind != CHECK_CONDITION_ENABLE && condition->state == CHECK_CONDITION_TRUE;
    }
    if (holds != NULL)
    {
        check_condition_diagnose(reader, entry, token, holds);
    }
    arena_pop_to(reader->scratch, mark);
    return entry->restricted && !check_condition_callee(reader->parser, token) && !check_condition_unevaluated(reader->parser, token);
}

// The operation that converts the function a restricted use at `token` names (clang's DefaultFunctionArrayConversion, which
// gives the address error), past the parentheses around the name: the use's parent; a call's `)` for an argument, which
// clang converts in order, the first failure ending the call; and an initializer's outermost `}` for an element (a
// designation's too), whose elements clang converts each on its own once the whole list is read (`*independent`).
static uint32_t check_condition_conversion(const Parser* parser, uint32_t token, bool* independent)
{
    const uint32_t count = parser->tokens.count;
    uint32_t at = parser->tree.parents[token];
    while (at < count && parser->tree.roles[at] == PARSE_ROLE_CLOSE_GROUP)
    {
        at = parser->tree.parents[at];
    }
    // Past an argument list's or an initializer list's commas, and an initializer's designations and enclosing lists.
    uint32_t above = at;
    uint32_t list = BUSTER_CHECK_NONE;
    bool climbing = true;
    while (climbing && above < count)
    {
        // (A comma is a SEPARATOR or a COMMA: its kind, which every comma has, says it.)
        const uint8_t role = parser->tree.roles[above];
        const bool comma = parser->tokens.kinds[above] == PARSE_KIND_COMMA;
        const uint32_t parent = parser->tree.parents[above];
        const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        const bool parent_comma = parent < count && parser->tokens.kinds[parent] == PARSE_KIND_COMMA;
        const uint32_t grandparent = parent < count ? parser->tree.parents[parent] : BUSTER_CHECK_NONE;
        const uint8_t grandparent_role = grandparent < count ? parser->tree.roles[grandparent] : PARSE_ROLE_NONE;
        const bool grandparent_comma = grandparent < count && parser->tokens.kinds[grandparent] == PARSE_KIND_COMMA;
        const bool element = parent_comma || parent_role == PARSE_ROLE_CLOSE_INITIALIZER ||
                             (parent_role == PARSE_ROLE_ASSIGN && (grandparent_comma || grandparent_role == PARSE_ROLE_CLOSE_INITIALIZER));
        const bool braces = role == PARSE_ROLE_CLOSE_INITIALIZER || role == PARSE_ROLE_CLOSE_COMPOUND_BODY;
        list = braces ? above : list;
        climbing = comma || (role == PARSE_ROLE_CLOSE_INITIALIZER && element) ||
                   (role == PARSE_ROLE_ASSIGN && (parent_comma || parent_role == PARSE_ROLE_CLOSE_INITIALIZER));
        above = climbing ? parent : above;
    }
    const uint8_t found = above < count ? parser->tree.roles[above] : PARSE_ROLE_NONE;
    *independent = list != BUSTER_CHECK_NONE;
    return list != BUSTER_CHECK_NONE ? list : found == PARSE_ROLE_CLOSE_CALL ? above : at;
}
// The address errors of the restricted uses at `uses` (in source order, `use_count` of them), as clang gives them: at the
// operation that converts each (check_condition_conversion), which clang builds only once its operands are, so not where
// one of them holds a failed conversion (`(q == f) + r`'s `r` has none); and, where it converts its operands in order, the
// first that fails ends it (`c ? q : r` gives `q`'s alone, as `g(q, r)` does). Each error is at the outermost `(` around
// the name. Marks a byte per token in the reader's scratch: bit 0 at an operation one of whose operands failed, bit 1 at one
// that converted a failed use.
static void check_condition_addresses(CheckInputReader* reader, const uint32_t* uses, uint32_t use_count)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    uint8_t* failed = use_count > 1 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count) : NULL;
    reader->output->resource |= use_count > 1 && failed == NULL;
    if (failed != NULL)
    {
        memset(failed, 0, count);
        // Each failed conversion makes its operation, and each one around it, fail up to its statement: bit 0 at their
        // parents. A walk stops where an earlier one marked the rest.
        for (uint32_t i = 0; i < use_count; i += 1)
        {
            bool independent = false;
            uint32_t at = check_condition_conversion(parser, uses[i], &independent);
            bool walking = at < count;
            while (walking)
            {
                const uint8_t role = parser->tree.roles[at];
                const uint32_t parent = parser->tree.parents[at];
                walking = parent < count && role != PARSE_ROLE_TERMINATOR && role != PARSE_ROLE_CLOSE_BLOCK && role != PARSE_ROLE_CLOSE_BODY &&
                          (failed[parent] & 1u) == 0;
                failed[parent < count ? parent : at] |= walking ? 1u : 0u;
                at = parent;
            }
        }
    }
    for (uint32_t i = 0; i < use_count && !reader->output->resource; i += 1)
    {
        const uint32_t token = uses[i];
        bool independent = false;
        const uint32_t at = check_condition_conversion(parser, token, &independent);
        const bool reported = failed == NULL || at >= count || ((failed[at] & 1u) == 0 && (independent || (failed[at] & 2u) == 0));
        if (failed != NULL && at < count)
        {
            failed[at] |= 2u;
        }
        if (reported)
        {
            // At the outermost `(` around the name, where clang's parenthesized expression begins.
            uint32_t first = token;
            uint32_t after = token;
            while (first > 0 && after + 1 < count && parser->tokens.kinds[first - 1] == PARSE_KIND_L_PAREN &&
                   parser->tree.roles[first - 1] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[first - 1] == after + 1)
            {
                first -= 1;
                after += 1;
            }
            const uint32_t before = reader->output->report_count;
            check_input_report(reader, first, CHECK_INPUT_ENABLE_IF_ADDRESS);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].value = token;
            }
            // clang's expression is invalid then, and so are its statement's or declaration's checks, which the later
            // expression checks leave from its start: back past the tokens of its depth or more to the end of what is before
            // it (a `;`, a block's `}`, a statement's head).
            const uint32_t depth = parser->tree.depths[token];
            uint32_t start = first;
            bool walking = true;
            while (walking && start > 0)
            {
                const uint32_t previous = start - 1;
                const uint8_t role = parser->tree.roles[previous];
                const bool ends = parser->tokens.kinds[previous] == PARSE_KIND_SEMI || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY ||
                                  role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO ||
                                  role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_BODY;
                walking = !(ends && parser->tree.depths[previous] <= depth);
                start -= walking ? 1u : 0u;
            }
            reader->expression_limit = start < reader->expression_limit ? start : reader->expression_limit;
        }
    }
    arena_pop_to(reader->scratch, mark);
}

// The uses of functions with conditions (above), once the unit's names are bound: each token the bindings resolved to a
// declaration with a list (CheckInput.declarations, not a declaration's own name). The vector path tests 16 tokens at a
// time, gathering each one's declaration's mark; the scalar path, its twin, one at a time.
static void check_input_condition_uses(CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    // Each declaration's mark, padded for the gathers' 4-byte reads.
    uint8_t* marked = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, (uint64_t)count + 4);
    // The uses whose address errors check_condition_addresses decides, at most one per token.
    uint32_t* addresses = marked != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1) : NULL;
    uint32_t address_count = 0;
    reader->output->resource |= addresses == NULL;
    if (addresses != NULL)
    {
        memset(marked, 0, (uint64_t)count + 4);
        for (uint32_t i = 0; i < input->condition_declaration_count; i += 1)
        {
            // An overloadable function's are the selected candidate's at a call (check_input_condition_call), and its
            // address is resolved among the candidates its enable_ifs leave eligible (check_overload_address).
            const CheckConditionDeclaration* entry = input->condition_declarations + i;
            const bool overloadable = (check_input_overload_flags(check_input_attributes_at(input, entry->declaration)) & BUSTER_CHECK_OVERLOADABLE) != 0;
            marked[entry->declaration] = (entry->independent || entry->restricted) && !overloadable ? 1u : 0u;
        }
        const bool vector = BUSTER_VECTOR_PATH(!parser->options.scalar) && count <= INT32_MAX;
        uint32_t first = 0;
        while (vector && first < count)
        {
            const uint32_t length = count - first < 16 ? count - first : 16;
            const __mmask16 valid = (__mmask16)(length == 16 ? 0xffffu : (1u << length) - 1u);
            const __m512i declarations = _mm512_maskz_loadu_epi32(valid, input->declarations + first);
            const __m512i tokens = _mm512_add_epi32(_mm512_set1_epi32((int32_t)first), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
            const __mmask16 bound = _mm512_mask_cmplt_epu32_mask(valid, declarations, _mm512_set1_epi32((int32_t)count)) &
                                    _mm512_cmpneq_epi32_mask(declarations, tokens);
            const __m512i marks = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), bound, declarations, marked, 1);
            for (uint32_t hits = _mm512_mask_test_epi32_mask(bound, marks, _mm512_set1_epi32(0xff)); hits != 0; hits &= hits - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(hits);
                if (parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && !check_input_declares(reader, token))
                {
                    const bool address = check_condition_use(reader, token, check_condition_declaration(input, input->declarations[token]));
                    addresses[address_count] = token;
                    address_count += address ? 1u : 0u;
                }
            }
            first += length;
        }
        for (uint32_t token = 0; !vector && token < count; token += 1)
        {
            const uint32_t declaration = input->declarations[token];
            if (declaration < count && declaration != token && marked[declaration] != 0 && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                !check_input_declares(reader, token))
            {
                const bool address = check_condition_use(reader, token, check_condition_declaration(input, declaration));
                addresses[address_count] = token;
                address_count += address ? 1u : 0u;
            }
        }
        check_condition_addresses(reader, addresses, address_count);
    }
    arena_pop_to(reader->scratch, mark);
}
