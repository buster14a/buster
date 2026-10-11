// Shared library for android/runtime_order: bionic runs its constructor after
// the executable's DT_PREINIT_ARRAY and before the executable's constructors.
// It needs no libc, so any Android-targeting C compiler can build it. The
// state is volatile so an optimizing compiler cannot evaluate the constructor
// at compile time and drop DT_INIT_ARRAY.
static volatile int dependency_counter;
static volatile int dependency_constructed;

__attribute__((constructor)) static void dependency_constructor(void)
{
    dependency_counter += 1;
    dependency_constructed = dependency_counter;
}

int runtime_order_tick(void)
{
    dependency_counter += 1;
    return dependency_counter;
}

int runtime_order_dependency_constructed(void)
{
    return dependency_constructed;
}
