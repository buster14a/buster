typedef struct { int a; } S;
S make(void);
__auto_type g = 1L;
static __auto_type h = 'c';
int f(void) {
    static __auto_type n = sizeof g;
    __auto_type s = make();
    __auto_type t = (S){ 2 };
    __auto_type e = (int[]){ 1, 2, 3 };
    __auto_type r = ({ __auto_type r = 4; r + 1; });
    int sum = s.a + t.a + h + e[2] + r;
    for (__auto_type i = 0u; i < n; i++) sum += (int)i;
    _Static_assert(__builtin_types_compatible_p(__typeof__(g), long), "file scope");
    _Static_assert(__builtin_types_compatible_p(__typeof__(h), int), "a character constant's int");
    _Static_assert(__builtin_types_compatible_p(__typeof__(n), unsigned long), "sizeof's size_t");
    _Static_assert(__builtin_types_compatible_p(__typeof__(s), S), "a structure");
    _Static_assert(__builtin_types_compatible_p(__typeof__(t), S), "a compound literal");
    _Static_assert(__builtin_types_compatible_p(__typeof__(e), int *), "an array's compound literal decays");
    __auto_type l = ({ int k = sum; if (k > 3) goto done; k = 5; done: k * 2L; });
    _Static_assert(__builtin_types_compatible_p(__typeof__(l), long), "a labeled last statement's");
    extern long g;
    return sum + (int)l + (int)g;
}
