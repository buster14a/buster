// The second translation unit of the cross-translation-unit constructor
// order fixture (issue 789); `tests/basic_c_constructor_order.c` holds `main`
// and the table this file's header comment refers to.  It owns the recorder
// and the array so a constructor in either unit writes the same object, and
// it registers the priorities that have to interleave with the other unit's:
// 101 ahead of everything, 150 behind the other unit's 120, and one that
// names no priority behind both unprioritized constructors of the program.

int constructor_order[8];
int constructor_order_count;

void record_constructor(int identifier)
{
    if (constructor_order_count < (int)(sizeof(constructor_order) / sizeof(constructor_order[0])))
    {
        constructor_order[constructor_order_count] = identifier;
    }
    constructor_order_count += 1;
}

__attribute__((constructor(101))) static void with_earliest_priority(void)
{
    record_constructor(1);
}

__attribute__((constructor(150))) static void with_latest_priority(void)
{
    record_constructor(3);
}

__attribute__((constructor)) static void second_without_priority(void)
{
    record_constructor(5);
}
