// Initializer spellings see macros at the declaration, including #undef and pop_macro (#2376).
int integer(void) { int value; return value; }
double floating(void) { double value; return value; }
int *before_define(void) { int *value; return value; }
#define NULL ((void *)0)
int *after_define(void) { int *value; return value; }
#pragma push_macro("NULL")
#undef NULL
int *after_undef(void) { int *value; return value; }
#pragma pop_macro("NULL")
int *after_restore(void) { int *value; return value; }
#undef NULL
int *defined_in_body(void)
{
    int *value;
#define NULL ((void *)0)
    return value;
}
int *undefined_in_body(void)
{
    int *value;
#undef NULL
    return value;
}
_Bool before_false(void) { _Bool value; return value; }
#define false 0
_Bool after_false(void) { _Bool value; return value; }
#pragma push_macro("false")
#undef false
_Bool without_false(void) { _Bool value; return value; }
#pragma pop_macro("false")
_Bool restored_false(void) { _Bool value; return value; }
#undef false
