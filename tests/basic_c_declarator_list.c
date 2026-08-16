// A file-scope declaration binds every declarator in its list, not just the
// first. The declaration parses either way, so the failure only ever shows up
// at the first *use* of a later name — which is why the whole point of this
// fixture is that every declared name below is read.

// Two scalars.
int two_scalars_first, two_scalars_second;

// A scalar followed by an array.
static unsigned long scalar_then_array_value, scalar_then_array_elements[8];

// A scalar followed by a pointer. The '*' belongs to the declarator, so the
// second name must not inherit the first one's type.
static unsigned long scalar_then_pointer_value, *scalar_then_pointer_target;

// An array ahead of a scalar: order is irrelevant, both names are declared.
static unsigned long array_then_scalar_elements[8], array_then_scalar_value;

// Initialized declarators. Each one owns its own initializer; reading the
// list as a single declaration would give the second name the first's.
int initialized_first = 1, initialized_second = 2;

// Mixed declarator shapes with initializers, including one whose bound is
// inferred from its own initializer.
static int mixed_scalar = 3, mixed_array[] = {4, 5, 6}, *mixed_pointer = &initialized_first;

// A declarator list of function declarations, and one of typedef names.
static int list_declared_add(int, int), list_declared_double(int);
typedef int DeclaredInt, *DeclaredIntPointer, DeclaredIntPair[2];

static int list_declared_add(int left, int right) { return left + right; }
static int list_declared_double(int value) { return value * 2; }

int main(void)
{
    two_scalars_first = 10;
    two_scalars_second = 20;
    if (two_scalars_first + two_scalars_second != 30)
    {
        return 1;
    }

    scalar_then_array_value = 7;
    scalar_then_array_elements[0] = 11;
    scalar_then_array_elements[7] = 13;
    if (scalar_then_array_value + scalar_then_array_elements[0] + scalar_then_array_elements[7] != 31)
    {
        return 2;
    }

    scalar_then_pointer_value = 17;
    scalar_then_pointer_target = &scalar_then_pointer_value;
    if (*scalar_then_pointer_target != 17)
    {
        return 3;
    }

    array_then_scalar_elements[3] = 19;
    array_then_scalar_value = 23;
    if (array_then_scalar_elements[3] + array_then_scalar_value != 42)
    {
        return 4;
    }

    if (initialized_first != 1 || initialized_second != 2)
    {
        return 5;
    }

    if (mixed_scalar != 3 || mixed_array[0] != 4 || mixed_array[2] != 6 || *mixed_pointer != 1)
    {
        return 6;
    }
    if (sizeof(mixed_array) / sizeof(mixed_array[0]) != 3)
    {
        return 7;
    }

    if (list_declared_add(2, 3) != 5 || list_declared_double(4) != 8)
    {
        return 8;
    }

    {
        DeclaredInt alias_scalar = 30;
        DeclaredIntPointer alias_pointer = &alias_scalar;
        DeclaredIntPair alias_pair = {40, 50};
        if (alias_scalar + *alias_pointer + alias_pair[0] + alias_pair[1] != 150)
        {
            return 9;
        }
    }

    // Block scope has always handled declarator lists; keep both paths in one
    // fixture so a change to either is compared against the other.
    {
        int local_first = 60, local_second = 70, local_array[2] = {80, 90}, *local_pointer = &local_first;
        if (local_first + local_second + local_array[0] + local_array[1] + *local_pointer != 360)
        {
            return 10;
        }
    }

    return 0;
}
