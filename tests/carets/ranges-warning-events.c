int index_character(int *values, char index)
{
    return values[index];
}

int index_reversed(int *values, char index)
{
    return index[values];
}

int index_grouped(int *values, char index)
{
    return values[(index)];
}

void *cast_integer(int value)
{
    return (void *)value;
}

int cast_pointer(int *value)
{
    return (int)value;
}

unsigned long array_parameter(int values[4])
{
    return sizeof(values);
}

int division(int value)
{
    return value / (1 - 1);
}

int bound_comparison[(1 == 2, 3)];

void void_dereference(void *pointer)
{
    (void)*pointer;
}

int ordered(int *pointer, long index)
{
    return pointer < index;
}

struct Record { int member; };
struct EmptyDeclaration { unsigned int; int member; };
struct Record same_cast(struct Record value)
{
    return (struct Record)value;
}

void unused(int parameter)
{
    int variable;
    typedef int local_type;
unused_label:;
}

static int unused_function(void)
{
    return 0;
}

void unused_set(void)
{
    int variable;
    variable = 2;
}

int *null_call(void)
{
    return (__builtin_expect(0, 0));
}

int *null_binary(void)
{
    return 1 - 1;
}

int *null_conditional(void)
{
    return __builtin_expect(0, 0) ? 0 : 0;
}
