static int values[1] = {4};
static int selected_index_calls;
static int discarded_index_calls;
static int sibling_calls;
static int selected_callee_calls;
static int discarded_callee_calls;

static int selected_index(void)
{
    selected_index_calls += 1;
    return 0;
}

static int discarded_index(void)
{
    discarded_index_calls += 1;
    return 0;
}

static int sibling(void)
{
    sibling_calls += 1;
    return 3;
}

static int accept_update_argument(int value)
{
    return value;
}

static int selected_callee(void)
{
    selected_callee_calls += 1;
    return 7;
}

static int discarded_callee(void)
{
    discarded_callee_calls += 1;
    return 9;
}

int main(void)
{
    int prefix = ++((_Generic(0, int: values[selected_index()], default: values[discarded_index()])));
    int postfix = ((_Generic(0, int: values[selected_index()], default: values[discarded_index()])))++ + sibling();
    int argument = accept_update_argument(_Generic(0, int: values[selected_index()], default: values[discarded_index()])++);
    int nested = _Generic(0,
                          int: _Generic(1, int: values[selected_index()], default: values[discarded_index()]),
                          default: values[discarded_index()])++;
    int called = _Generic(0, int: selected_callee, default: discarded_callee)();
    return !(prefix == 5 && postfix == 8 && argument == 6 && nested == 7 && values[0] == 8 &&
             selected_index_calls == 4 && discarded_index_calls == 0 && sibling_calls == 1 &&
             selected_callee_calls == 1 && discarded_callee_calls == 0 && called == 7);
}
