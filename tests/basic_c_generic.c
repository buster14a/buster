static int selected_calls;
static int unselected_calls;

static int selected(void)
{
    selected_calls += 1;
    return 17;
}

static int unselected(void)
{
    unselected_calls += 1;
    return 99;
}

int main(void)
{
    int control = 0;
    double floating = 2.0;
    int* pointer = &control;
    int first = _Generic(control++, int: selected(), default: unselected());
    int second = _Generic(floating, int: unselected(), double: 23, default: (control ? unselected() : unselected()));
    int third = 3 + _Generic(pointer, int*: 29, default: unselected());
    int nested = _Generic(_Generic(floating, double: control, default: floating), int: 31, default: unselected());
    int string_type = _Generic("buster", char*: 37, default: unselected());
    return first != 17 || second != 23 || third != 32 || nested != 31 || string_type != 37 || control != 0 || selected_calls != 1 || unselected_calls != 0;
}
