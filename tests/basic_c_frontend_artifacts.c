typedef struct Pair
{
    int left;
    int right;
} Pair;

typedef union Value
{
    int integer;
    unsigned char bytes[4];
} Value;

static int callback(int value);

static int callback_two(int value)
{
    return value + 2;
}

static int (*callbacks[2])(int) = {callback, callback_two};
static const int constant = (3 * 4) + (5 > 2 ? 1 : 0);
static Pair pair = {.right = 9, .left = 4};
static Value value = {.bytes = {1, 2, 3, 4}};
static int matrix[2][3] = {{1, 2, 3}, 4, 5, 6};
static char exact[3] = "abc";
static int* address = &matrix[1][2];
static int pointer_target;
static int *const pointer_source = &pointer_target;
static int *pointer_alias = pointer_source;
static int short_false = 0 && (1 / 0);
static int short_true = 1 || (1 / 0);
static int short_conditional = 0 ? (1 / 0) : 7;

static int callback(int value)
{
    return value + pair.left;
}

static int parameter_sum(int values[2][3])
{
    return values[0][0] + values[1][2];
}

static int apply(int (*)(int), int values[2][3])
{
    return callbacks[0](values[0][0]) + values[1][2];
}

int external_value = 3;

static int local_static_and_extern(void)
{
    static Pair local_pair = {.right = 3, .left = 1};
    extern int external_value;
    return local_pair.left + external_value;
}

static int statement_and_builtins(int input, int values[2][3])
{
    int local_values[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int chosen = __builtin_choose_expr(__builtin_constant_p(1 + 2), 7, input);
    int runtime_constant = __builtin_constant_p(input);
    int compatible = __builtin_types_compatible_p(int, int);
    unsigned long object_size = __builtin_object_size(local_values, 0);
    int (*aligned)[3] = __builtin_assume_aligned(local_values, 16);
    return chosen + runtime_constant + compatible + (object_size == sizeof(local_values)) + (aligned[1][2] == values[1][2]) + ({
        int local = input + 1;
        local * 2;
    });
}

int main(void)
{
    int values[2][3] = {{1, 2, 3}, {4, 5, 6}};
    if (callbacks[0](1) != 5 || callbacks[1](1) != 3)
    {
        return 1;
    }
    if (constant != 13 || pair.left != 4 || pair.right != 9)
    {
        return 2;
    }
    if (value.bytes[0] != 1 || value.bytes[3] != 4 || matrix[1][2] != 6 || *address != 6 || pointer_alias != &pointer_target)
    {
        return 3;
    }
    if (exact[0] != 'a' || exact[1] != 'b' || exact[2] != 'c')
    {
        return 4;
    }
    if (short_false != 0 || short_true != 1 || short_conditional != 7 || parameter_sum(values) != 7 || apply(callback, values) != 11 ||
        local_static_and_extern() != 4 || statement_and_builtins(3, values) != 18)
    {
        return 5;
    }
    return 0;
}
