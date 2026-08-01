struct Pair
{
    int first;
    long second;
};

static int global_value = 7;

static unsigned long produce_value(void)
{
    return 17ul;
}

int main(void)
{
    const int qualified = 3;
    typeof(qualified) same_qualification = qualified;
    typeof_unqual(qualified) mutable_value = qualified;
    typeof(int*) pointer_from_type = &global_value;
    typeof(&global_value) pointer_from_expression = &global_value;
    typeof(*pointer_from_expression) copied_value = *pointer_from_expression;
    struct Pair pair = {5, 11};
    typeof(pair.second) field_value = pair.second;
    int values[3] = {2, 3, 5};
    typeof(values) copied_values = {7, 11, 13};
    typeof(values[0]) element_value = values[2];
    typeof(1 + 2l) arithmetic_value = 19;
    typeof(1.0f + 2) floating_value = 3.5f;
    typeof(produce_value()) call_value = produce_value();
    typeof((short)global_value) cast_value = 7;
    typeof(global_value ? 1l : 2l) conditional_value = global_value ? 23 : 29;
    typeof((global_value, 31ul)) comma_value = 31;
    typeof(sizeof(values)) size_value = sizeof(values);
    typeof("abc") text_value = "abc";

    mutable_value += 1;
    return same_qualification == 3 && mutable_value == 4 && *pointer_from_type == 7 && *pointer_from_expression == 7 && copied_value == 7 &&
                   field_value == 11 && sizeof(copied_values) == sizeof(values) && copied_values[2] == 13 && element_value == 5 && arithmetic_value == 19 &&
                   floating_value == 3.5f && call_value == 17ul && cast_value == 7 && conditional_value == 23 && comma_value == 31ul &&
                   size_value == sizeof(values) && sizeof(text_value) == 4 && text_value[2] == 'c'
               ? 0
               : 1;
}
