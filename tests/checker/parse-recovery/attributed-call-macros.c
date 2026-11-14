#define ATTR __attribute__((unused))
#define CALLEE missing_macro
#define REJECTED_CALL(name) ATTR name(unread_macro_argument)

void first(void)
{
    ATTR CALLEE(unread_one);
    REJECTED_CALL(missing_other);
    ATTR CALLEE(unread_two);
    after_macros;
}

void second(void)
{
    REJECTED_CALL(missing_macro);
    after_second;
}
