struct Pair { int first; int second; };
#if ABI_TEST_MODE == 0
struct Pair return_pair(int value)
{
    struct Pair result = {value, value + 1};
    return result;
}
#elif ABI_TEST_MODE == 1
extern int take_pair(struct Pair value);
int direct_call(int value)
{
    struct Pair argument = {value, value + 1};
    return take_pair(argument);
}
#elif ABI_TEST_MODE == 2
int indirect_call(int (*function)(struct Pair), struct Pair *value)
{
    return function(*value);
}
#elif ABI_TEST_MODE == 3
int scalar_signature(int value)
{
    struct Pair local = {value, value + 1};
    return local.first + local.second;
}
#elif ABI_TEST_MODE == 4
extern int variadic_pair(int tag, ...);
int aggregate_variadic(int value)
{
    struct Pair local = {value, value + 1};
    return variadic_pair(value, local);
}
#elif ABI_TEST_MODE == 5
struct Empty {};
extern int empty_signature(struct Empty value);
int marker(void)
{
    return 0;
}
#else
struct Padding { int :32; };
extern int take_padding(struct Padding value);
int padding_only(struct Padding *value)
{
    return take_padding(*value);
}
#endif
