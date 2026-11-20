struct Value { int value; };
int input;
int *file_scalar = (int){ input };
struct Value file_copy = (struct Value){ input };

void compound_storage(void)
{
    int scalar = (int){ input };
    struct Value record = (struct Value){ input };
    int *independent_conversion = (int){ input };
    (void)scalar;
    (void)record;
}
