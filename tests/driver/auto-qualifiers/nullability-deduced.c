typedef int Number;
typedef struct Item
{
    int value;
} Item;

int object;
const int constant = 0;

__auto_type _Nullable scalar = 0;
_Nonnull __auto_type wide = 0L;
__auto_type _Null_unspecified number = (Number)0;
__auto_type _Nullable_result record = (Item){ 0 };
__auto_type _Nullable *pointee = &constant;
__auto_type _Nullable a = 0, b = 0L;
__auto_type _Nullable c = &object, d = 0;

#define REQUIRED _Nonnull

void local(void)
{
    __auto_type REQUIRED bad = 0;
    bad = "invalid declaration's uses are quiet";
}
