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
// address" error. An overloadable function's conditions, which take part in overload resolution, are left unsupported.
//
// A unit without these attributes pays a flag test (CheckInputReader.condition_attributes) where declarations are read
// and a count test where uses and calls are.

#define BUSTER_CHECK_CONDITION_CONSTANT 0xffffffffu
#define BUSTER_CHECK_CONDITION_PARAMETER 0xfffffffeu

typedef enum CheckConditionKind
{
    CHECK_CONDITION_WARNING,
    CHECK_CONDITION_ERROR,
    CHECK_CONDITION_ENABLE,
} CheckConditionKind;

typedef enum CheckConditionState
{
    // A condition that names no parameter, false or true once and for all.
    CHECK_CONDITION_FALSE,
    CHECK_CONDITION_TRUE,
    // A condition each call evaluates with its arguments (CheckCondition.steps).
    CHECK_CONDITION_DEPENDENT,
    // A system header's condition whose shape the steps don't take: it decides nothing.
    CHECK_CONDITION_OPAQUE,
} CheckConditionState;

// One step of a condition's program, in postorder: a constant (`index` into CheckCondition.constants), a parameter (its
// place `index`, of type `type`) or a CheckOp on earlier steps' values (`operands`, a cast's destination `type`).
typedef struct CheckConditionStep CheckConditionStep;
struct CheckConditionStep
{
    uint32_t op;
    uint32_t operands[3];
    uint32_t index;
    TypeId type;
};

typedef struct CheckCondition CheckCondition;
struct CheckCondition
{
    const CheckConditionStep* steps;
    const CheckValue* constants;
    uint32_t step_count;
    // The attribute's first token (its note's place), the message's first string token and the declaration's name.
    uint32_t attribute;
    uint32_t message;
    uint32_t declaration;
    uint8_t kind;
    uint8_t state;
};

typedef struct CheckConditionDeclaration CheckConditionDeclaration;
struct CheckConditionDeclaration
{
    uint32_t declaration;
    uint32_t count;
    const uint32_t* conditions;
    // Whether some diagnose_if holds at every use, some enable_if isn't always true, and some condition needs a call's
    // arguments.
    bool independent;
    bool restricted;
    bool dependent;
};

// The list of declaration `declaration` (CheckInput.condition_declarations, by binary search), or NULL.
static const CheckConditionDeclaration* check_condition_declaration(const CheckInput* input, uint32_t declaration)
{
    uint32_t low = 0;
    uint32_t high = input->condition_declaration_count;
    const CheckConditionDeclaration* result = NULL;
    while (result == NULL && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const uint32_t at = input->condition_declarations[middle].declaration;
        result = at == declaration ? input->condition_declarations + middle : NULL;
        high = declaration < at ? middle : high;
        low = declaration > at ? middle + 1 : low;
    }
    return result;
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

// clang's "'X' attribute expression never produces a constant expression" at the attribute, with its note at `note`.
static void check_condition_never_constant(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t note)
{
    const uint32_t before = reader->output->report_count;
    check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_NEVER_CONSTANT, 0);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].note_token = note;
        reader->output->reports[before].note_kind = note != BUSTER_CHECK_NONE ? CHECK_INPUT_NOTE_CONSTANT_SUBEXPRESSION : CHECK_INPUT_NOTE_NONE;
    }
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
    CheckConditionStep* steps = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckConditionStep, n);
    CheckValue* constants = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, n);
    const bool allocated = first_child != NULL && last_child != NULL && next_sibling != NULL && order != NULL && stack != NULL && cursor != NULL && low != NULL &&
                           high != NULL && step_of != NULL && named != NULL && constant_node != NULL && skipped != NULL && steps != NULL && constants != NULL;
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
        skipped[v] = !top && (skipped[parent - first] || constant_node[parent - first] || opener || type_name);
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
            bool avoidable = false;
            for (uint32_t at = token; read && !evaluated && !avoidable && parser->tree.parents[at] >= first && parser->tree.parents[at] < end &&
                 parser->tree.parents[at] != at; at = parser->tree.parents[at])
            {
                const uint32_t above = parser->tree.parents[at];
                const uint8_t above_kind = parser->tokens.kinds[above];
                avoidable = ((above_kind == PARSE_KIND_AMPAMP || above_kind == PARSE_KIND_PIPEPIPE) && at > above) ||
                            (above_kind == PARSE_KIND_COLON && first_child[above - first] != at - first);
            }
            if (!evaluated && variable != BUSTER_CHECK_NONE && !avoidable)
            {
                check_condition_never_constant(reader, attributes, attribute, variable);
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
        else if (!skipped[v] && kind == PARSE_KIND_IDENTIFIER && first_child[v] == BUSTER_CHECK_NONE)
        {
            const uint32_t place = check_condition_parameter(reader, parameters, list, token);
            result = place < parameter_count ? result : CHECK_CONDITION_UNSUPPORTED;
            if (result == CHECK_CONDITION_BUILT)
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
    // The names, a function parameter's first.
    bool dependent = false;
    for (uint32_t t = first; valid && t < end; t += 1)
    {
        const uint8_t previous = t > 0 ? parser->tokens.kinds[t - 1] : PARSE_KIND_END;
        if (parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && previous != PARSE_KIND_PERIOD && previous != PARSE_KIND_ARROW)
        {
            (void)check_input_thread_name(reader, parameters, NULL, 0, NULL, 0, t);
            dependent |= check_condition_parameter(reader, parameters, list, t) != BUSTER_CHECK_NONE;
        }
    }
    CheckValue value = { 0 };
    bool call_failure = false;
    valid = valid && check_input_attribute_expression(reader, attributes, attribute, first, end, true, &value, &call_failure);
    CheckConditionBuild result = valid ? CHECK_CONDITION_BUILT : CHECK_CONDITION_FAILED;
    if (valid && !dependent)
    {
        bool truth = false;
        const bool known = value.constant_kind != CHECK_CONSTANT_NONE && check_input_const_rvalue(reader, end, &value) && check_action_truth(reader->types, &value, &truth);
        condition->state = truth ? CHECK_CONDITION_TRUE : CHECK_CONDITION_FALSE;
        if (!known)
        {
            check_condition_never_constant(reader, attributes, attribute, check_condition_variable(reader, parameters, list, first, end));
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

// The conditions of function declaration `name` (above): its own attributes' (`attributes`, null for none) after those
// its previous declaration `previous` has, the list kept where any holds. The function's type is `type`, its parameters'
// names `parameters` and its parameter list's `(` `list`. False after an error.
static bool check_input_conditions(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t name, TypeId type, uint32_t parameters, uint32_t list,
                                   uint32_t previous)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const TypeId function = type_strip_aligned(reader->types, type);
    const CheckConditionDeclaration* inherited = previous < parser->tokens.count && input->condition_declaration_count != 0 ?
                                                 check_condition_declaration(input, previous) : NULL;
    const uint32_t inherited_count = inherited != NULL ? inherited->count : 0;
    uint32_t own = 0;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        own += (attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF || attribute->kind == CHECK_ATTRIBUTE_ENABLE_IF) && !attribute->ignored && !attribute->subject_ignored ? 1u : 0u;
    }
    // #1361's object-size wrappers keep their conditions unevaluated, as before.
    const bool sized = attributes != NULL && attributes->object_sizes != NULL;
    const bool overloadable = attributes != NULL && (check_input_overload_flags(attributes) & BUSTER_CHECK_OVERLOADABLE) != 0;
    const bool system = check_graph_system_token(&input->graph, name);
    uint32_t* list_indices = own + inherited_count != 0 && !sized ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, own + inherited_count) : NULL;
    input->resource |= own + inherited_count != 0 && !sized && list_indices == NULL;
    bool valid = !input->resource;
    uint32_t count = 0;
    if (list_indices != NULL && inherited_count != 0)
    {
        memcpy(list_indices, inherited->conditions, (uint64_t)inherited_count * sizeof(uint32_t));
        count = inherited_count;
    }
    for (uint32_t i = 0; valid && list_indices != NULL && attributes != NULL && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        if ((attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF || attribute->kind == CHECK_ATTRIBUTE_ENABLE_IF) && !attribute->ignored && !attribute->subject_ignored)
        {
            CheckCondition condition;
            const CheckConditionBuild built = check_condition_build(reader, attributes, attribute, parameters, list, function, name, system, &condition);
            // An overloadable function's conditions take part in its overload resolution, which isn't supported; a system
            // header's are kept and decide nothing.
            const bool trivial = condition.kind == CHECK_CONDITION_ENABLE && condition.state == CHECK_CONDITION_TRUE;
            const bool unsupported = built == CHECK_CONDITION_UNSUPPORTED || (built == CHECK_CONDITION_BUILT && overloadable && !trivial && !system);
            if (unsupported)
            {
                check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            }
            valid = built == CHECK_CONDITION_BUILT && !unsupported;
            condition.state = overloadable && !trivial ? CHECK_CONDITION_OPAQUE : condition.state;
            const uint32_t index = valid ? check_condition_append(reader, &condition) : BUSTER_CHECK_NONE;
            valid &= index != BUSTER_CHECK_NONE;
            if (valid)
            {
                list_indices[count++] = index;
            }
        }
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

// Whether the name at `token` is in a sizeof's, an alignof's or a typeof's operand: a walk back over its statement, through
// complete groups, to each `(` open around it and the keyword before it.
static bool check_condition_unevaluated(const Parser* parser, uint32_t token)
{
    bool unevaluated = token > 0 && check_condition_unevaluated_keyword(parser, token - 1);
    uint32_t depth = 0;
    bool walking = true;
    for (uint32_t t = token; !unevaluated && walking && t > 0; t -= 1)
    {
        const uint8_t kind = parser->tokens.kinds[t - 1];
        walking = depth != 0 || (kind != PARSE_KIND_SEMI && kind != PARSE_KIND_L_BRACE && kind != PARSE_KIND_R_BRACE);
        depth += kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE ? 1u : 0u;
        const bool open = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        unevaluated = walking && open && depth == 0 && kind == PARSE_KIND_L_PAREN && t > 1 && check_condition_unevaluated_keyword(parser, t - 2);
        depth -= open && depth != 0 ? 1u : 0u;
    }
    return unevaluated;
}

// diagnose_if's diagnostics of `entry`'s conditions that `holds` says hold (its ith, the list's ith), at `location`: the
// first error alone, else each warning, each with its note at its attribute.
static void check_condition_diagnose(CheckInputReader* reader, const CheckConditionDeclaration* entry, uint32_t location, const bool* holds)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    uint32_t error = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; error == BUSTER_CHECK_NONE && i < entry->count; i += 1)
    {
        error = holds[i] && input->conditions[entry->conditions[i]].kind == CHECK_CONDITION_ERROR ? entry->conditions[i] : BUSTER_CHECK_NONE;
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
        if (holds[i] && condition->kind == CHECK_CONDITION_WARNING)
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_USER_DEFINED_WARNINGS,
                .kind = CHECK_WARNING_EVENT_DIAGNOSE_IF,
                .token = location,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = location + 1,
                .payload = { .name = parser->tokens.names[condition->declaration], .token = condition->message },
            };
            const CheckWarningNote note =
            {
                .kind = CHECK_WARNING_NOTE_DIAGNOSE_IF,
                .token = condition->attribute,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .name = parser->tokens.names[condition->declaration], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_event(reader, &event, &note, 1);
        }
    }
}

// One use of a function with conditions, at `token`, of `entry`'s declaration: the diagnose_if conditions that name no
// parameter and hold, and the address of a function an enable_if restricts.
static void check_condition_use(CheckInputReader* reader, uint32_t token, const CheckConditionDeclaration* entry)
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
    if (entry->restricted && !check_condition_callee(reader->parser, token) && !check_condition_unevaluated(reader->parser, token))
    {
        // At the outermost `(` around the name, where clang's parenthesized expression begins.
        uint32_t at = token;
        uint32_t after = token;
        while (at > 0 && after + 1 < reader->parser->tokens.count && reader->parser->tokens.kinds[at - 1] == PARSE_KIND_L_PAREN &&
               reader->parser->tree.roles[at - 1] == PARSE_ROLE_OPEN_GROUP && reader->parser->tree.parents[at - 1] == after + 1)
        {
            at -= 1;
            after += 1;
        }
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, at, CHECK_INPUT_ENABLE_IF_ADDRESS);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].value = token;
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
    reader->output->resource |= marked == NULL;
    if (marked != NULL)
    {
        memset(marked, 0, (uint64_t)count + 4);
        for (uint32_t i = 0; i < input->condition_declaration_count; i += 1)
        {
            const CheckConditionDeclaration* entry = input->condition_declarations + i;
            marked[entry->declaration] = entry->independent || entry->restricted ? 1u : 0u;
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
                    check_condition_use(reader, token, check_condition_declaration(input, input->declarations[token]));
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
                check_condition_use(reader, token, check_condition_declaration(input, declaration));
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}
