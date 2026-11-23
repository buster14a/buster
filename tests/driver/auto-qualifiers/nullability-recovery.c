__auto_type _Nullable bad = 0;
extern double bad;

__auto_type _Nullable (*returns)(void) = (int (*)(void))0;
__auto_type _Nullable (*returns_valid)(void) = (int* (*)(void))0;
__auto_type _Nullable no_value = (void)0;

void local(void)
{
    __auto_type _Nullable invalid = 0;
    __auto_type _Nonnull derived = invalid;
    derived = "invalid initializer's uses are quiet";
}
