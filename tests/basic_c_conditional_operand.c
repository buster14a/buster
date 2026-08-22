// The untaken arm of a conditional, and the unevaluated right operand of &&
// or ||, may not run -- including in the operand positions whose lowering
// hoists calls out of the surrounding expression before evaluating it
// (c_ir_prepare_calls_discover in c_gen.c). A call argument and a compound
// literal item are the two such positions: both used to evaluate every call
// in both arms and then select the right value, so only a side effect made
// the miscompile visible. Each check runs the shape twice, once with the
// branch not taken (the call must not run) and once with it taken (the call
// must run exactly once and its value must arrive).
static int calls;

static int make(int payload)
{
    calls += 1;
    return payload;
}

static int take(int value)
{
    return value;
}

static int take_two(int first, int second)
{
    return first * 100 + second;
}

typedef struct Pair
{
    int a;
    int b;
} Pair;

static int argument_conditional(int masked, int value)
{
    return take(masked ? make(value) : 0);
}

static int argument_conditional_false_arm(int masked, int value)
{
    return take(masked ? 0 : make(value));
}

static int argument_first_of_two(int masked, int value)
{
    return take_two(masked ? make(value) : 0, 7);
}

static int argument_second_of_two(int masked, int value)
{
    return take_two(7, masked ? make(value) : 0);
}

static int argument_nested_call(int masked, int value)
{
    return take(take(masked ? make(value) : 0));
}

static int argument_logical_and(int masked, int value)
{
    return take(masked && make(value));
}

static int argument_logical_or(int masked, int value)
{
    return take(masked || make(value));
}

static int compound_literal_designated(int masked, int value)
{
    Pair pair = (Pair){.a = 1, .b = masked ? make(value) : 0};
    return pair.b;
}

static int compound_literal_positional(int masked, int value)
{
    Pair pair = (Pair){1, masked ? make(value) : 0};
    return pair.b;
}

static int compound_literal_array(int masked, int value)
{
    int* items = (int[2]){1, masked ? make(value) : 0};
    return items[1];
}

static int compound_literal_logical_and(int masked, int value)
{
    Pair pair = (Pair){.a = 1, .b = masked && make(value)};
    return pair.b;
}

// The shapes that already branched, kept so a future change to call
// preparation cannot fix the two above by breaking these.
static int statement_conditional(int masked, int value)
{
    int result = 0;
    result += masked ? make(value) : 0;
    return result;
}

static int declaration_initializer(int masked, int value)
{
    Pair pair = {.a = 1, .b = masked ? make(value) : 0};
    return pair.b;
}

static int parenthesized_argument(int masked, int value)
{
    return take((masked ? make(value) : 0));
}

static int index_conditional(int masked, int value)
{
    static int table[4] = {0, 10, 20, 30};
    return table[masked ? make(value) : 0];
}

// A fully parenthesized control expression in these positions is lowered by
// the control-expression prepass and then must be *reused* by the operand's
// own lowering: without the reuse the condition is lowered twice, and a side
// effect in the condition runs twice while side effects in the arms stay
// correct -- which is why these shapes count the condition, not the arm.
static int conditions;

static int count_condition(int value)
{
    conditions += 1;
    return value;
}

static int parenthesized_condition_increment(int value)
{
    conditions = 0;
    return take((conditions++ ? 0 : make(value)));
}

static int double_parenthesized_condition_increment(int value)
{
    conditions = 0;
    return take(((conditions++ ? 0 : make(value))));
}

static int parenthesized_condition_logical_and(int masked)
{
    conditions = 0;
    return take((conditions++ && make(masked)));
}

static int parenthesized_condition_call(int masked, int value)
{
    conditions = 0;
    return take((count_condition(masked) ? make(value) : 0));
}

static int compound_literal_condition_increment(int value)
{
    conditions = 0;
    Pair pair = (Pair){.a = 1, .b = (conditions++ ? 0 : make(value))};
    return pair.b;
}

// The parenthesized group as a *condition* rather than a whole operand: the
// left operand of &&, an if condition, and a negation each lower through the
// condition frames, which reuse the prepared group the same way.
static int condition_position_logical_and(int value)
{
    conditions = 0;
    return take((conditions++ ? 1 : 1) && make(value));
}

static int condition_position_if(void)
{
    conditions = 0;
    int selected = 0;
    if ((conditions++ ? 1 : 1))
    {
        selected = 5;
    }
    return selected;
}

static int condition_position_negated(void)
{
    conditions = 0;
    return take(!(conditions++ ? 1 : 0));
}

// A nested call whose argument is a parenthesized control group: the
// argument expression of the outer call pushes its own control-expression
// prepass over a range containing the already-prepared group, which must
// jump past it rather than rescan its interior and run the condition again.
static int nested_call_prepared_group(int value)
{
    conditions = 0;
    return take(take(((conditions++ ? 0 : 0) || make(value))));
}

// Both directions of every shape share one failure code, so the exit status
// names the shape rather than the symptom.
static int check(int (*shape)(int, int), int not_taken_result, int taken_result, int code)
{
    int before = calls;
    if (shape(0, 3) != not_taken_result || calls != before)
    {
        return code;
    }
    if (shape(1, 3) != taken_result || calls != before + 1)
    {
        return code;
    }
    return 0;
}

int main(void)
{
    int failure = check(argument_conditional, 0, 3, 1);
    if (failure)
    {
        return failure;
    }
    // The false arm is the one that runs here, so the roles are swapped.
    int before = calls;
    if (argument_conditional_false_arm(1, 3) != 0 || calls != before)
    {
        return 2;
    }
    if (argument_conditional_false_arm(0, 3) != 3 || calls != before + 1)
    {
        return 2;
    }
    failure = check(argument_first_of_two, 7, 307, 3);
    if (failure)
    {
        return failure;
    }
    failure = check(argument_second_of_two, 700, 703, 4);
    if (failure)
    {
        return failure;
    }
    failure = check(argument_nested_call, 0, 3, 5);
    if (failure)
    {
        return failure;
    }
    failure = check(argument_logical_and, 0, 1, 6);
    if (failure)
    {
        return failure;
    }
    // || is the mirror image: the right operand runs when the left is false.
    before = calls;
    if (argument_logical_or(1, 3) != 1 || calls != before)
    {
        return 7;
    }
    if (argument_logical_or(0, 3) != 1 || calls != before + 1)
    {
        return 7;
    }
    failure = check(compound_literal_designated, 0, 3, 8);
    if (failure)
    {
        return failure;
    }
    failure = check(compound_literal_positional, 0, 3, 9);
    if (failure)
    {
        return failure;
    }
    failure = check(compound_literal_array, 0, 3, 10);
    if (failure)
    {
        return failure;
    }
    failure = check(compound_literal_logical_and, 0, 1, 11);
    if (failure)
    {
        return failure;
    }
    failure = check(statement_conditional, 0, 3, 12);
    if (failure)
    {
        return failure;
    }
    failure = check(declaration_initializer, 0, 3, 13);
    if (failure)
    {
        return failure;
    }
    failure = check(parenthesized_argument, 0, 3, 14);
    if (failure)
    {
        return failure;
    }
    failure = check(index_conditional, 0, 30, 15);
    if (failure)
    {
        return failure;
    }
    // The parenthesized shapes: each must observe exactly one condition
    // evaluation, and the value selected by that single evaluation.
    if (parenthesized_condition_increment(3) != 3 || conditions != 1)
    {
        return 16;
    }
    if (double_parenthesized_condition_increment(3) != 3 || conditions != 1)
    {
        return 17;
    }
    if (parenthesized_condition_logical_and(5) != 0 || conditions != 1)
    {
        return 18;
    }
    before = calls;
    if (parenthesized_condition_call(1, 4) != 4 || conditions != 1 || calls != before + 1)
    {
        return 19;
    }
    if (compound_literal_condition_increment(6) != 6 || conditions != 1)
    {
        return 20;
    }
    before = calls;
    if (condition_position_logical_and(1) != 1 || conditions != 1 || calls != before + 1)
    {
        return 21;
    }
    if (condition_position_if() != 5 || conditions != 1)
    {
        return 22;
    }
    // The condition is false on its single evaluation, so the negation
    // selects one.
    if (condition_position_negated() != 1 || conditions != 1)
    {
        return 23;
    }
    before = calls;
    if (nested_call_prepared_group(1) != 1 || conditions != 1 || calls != before + 1)
    {
        return 24;
    }
    return 0;
}
