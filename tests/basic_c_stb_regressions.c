// Focused frontend regressions reduced from the pinned stb compatibility
// harness.  Keep these as ordinary hosted C so the driver suite can compile
// and execute them without any third-party include or data dependency.
#include <stddef.h>

typedef struct StbRegressionHolder StbRegressionHolder;
struct StbRegressionHolder
{
    unsigned char buffer[8];
    int used;
};

typedef struct StbRegressionEntry StbRegressionEntry;
struct StbRegressionEntry
{
    int key;
    int value;
};

typedef struct StbRegressionArrayEntry StbRegressionArrayEntry;
struct StbRegressionArrayEntry
{
    int key[2];
    int value;
};

static void
stb_regression_copy(void *context, void *data, int size)
{
    unsigned char *destination = (unsigned char *)context;
    unsigned char *source = (unsigned char *)data;
    for (int index = 0; index < size; index += 1)
    {
        destination[index] = source[index];
    }
}

static int
stb_regression_address_of_array(void)
{
    StbRegressionHolder holder = {{11, 22, 33, 44, 0, 0, 0, 0}, 4};
    unsigned char output[8] = {0};
    // `holder.buffer` remains an aggregate PLACE in IR; taking its address
    // must not require a scalar LOAD materialization.
    stb_regression_copy(output, &holder.buffer, holder.used);
    // Also cover a direct local array, the shape used by stb_image_write's
    // callback flush path.
    unsigned char local[4] = {55, 66, 77, 88};
    stb_regression_copy(output, &local, 4);
    return output[0] == 55 && output[1] == 66 && output[2] == 77 && output[3] == 88;
}

static int
stb_regression_complex_postfix(void)
{
    int storage[4] = {10, 0, 30, 40};
    int *data = storage + 2;
    // The indexed postfix base is a parenthesized pointer subtraction.  Its
    // result is a load, but the ++ still needs the source place recovered from
    // that load so storage[1] can be updated in place.
    int value = data[((int *)data - 2)[1]++];
    return value == 30 && storage[1] == 1;
}

static void
stb_regression_mark(int *counter)
{
    *counter += 1;
}

static int
stb_regression_conditional_comma(void)
{
    int counter = 0;
    // The comma belongs to the true arm of ?:, not to the enclosing full
    // expression.  Conditional-aware comma splitting must retain that shape.
    int selected = (1 ? stb_regression_mark(&counter), 7 : 3);
    int alternate = (0 ? stb_regression_mark(&counter), 9 : 4);
    return selected == 7 && alternate == 4 && counter == 1;
}

static int
stb_regression_typeof_and_sizeof(void)
{
    StbRegressionEntry entry = {17, 23};
    StbRegressionEntry *map = &entry;
    // stb_ds spells member types this way inside its hmget/hmput macros.
    __typeof__((map)->key) key = map->key;
    // The operand after sizeof is a parenthesized expression followed by a
    // postfix member access, not a parenthesized type name.
    size_t key_size = sizeof (map)->key;
    StbRegressionArrayEntry array_entry = {{31, 47}, 59};
    StbRegressionArrayEntry *array_map = &array_entry;
    __typeof__((array_map)->key) *array_key = &array_map->key;
    size_t array_key_size = sizeof (array_map)->key;
    return key == 17 && key_size == sizeof(int) && array_key == &array_entry.key && array_key_size == sizeof(array_entry.key);
}

static int
stb_regression_void_assignment_comma(void)
{
    StbRegressionEntry entry = {5, 31};
    StbRegressionEntry *map = 0;
    // A void-cast assignment followed by a comma must preserve the call (or
    // address) result in map; the second operand is not another assignment.
    int observed = ((void)(map = &entry, 7), map->value);
    return observed == 31 && map == &entry;
}

int
main(void)
{
    return !(stb_regression_address_of_array() && stb_regression_complex_postfix() && stb_regression_conditional_comma() &&
             stb_regression_typeof_and_sizeof() && stb_regression_void_assignment_comma());
}
