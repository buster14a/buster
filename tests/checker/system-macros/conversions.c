// #3338: a common warning is suppressed by its token's expansion location, as clang's DiagnosticsEngine does, so a value a
// system header's macro gives user code warns; a conversion whose context (the `=`, the call, the `return`) a system
// header's macro writes is quiet, as CheckImplicitConversion's isInSystemMacro(CC) leaves it. User macros are controls.
# 1 "system-conversions.h" 3
#define SYSTEM_PI 3.14159265358979323846
#define SYSTEM_BIG 300
#define SYSTEM_WIDE 4294967295u
#define SYSTEM_SET(x, v) ((x) = (v))
#define SYSTEM_CALL(f, v) f(v)
#define SYSTEM_RETURN(v) return v
#define SYSTEM_DECLARE(n, v) char n = v
# 10 "conversions.c"
#define USER_SET(x, v) ((x) = (v))
void take(char);
char convert(int i, char c, _Bool b, int flag)
{
    i = SYSTEM_PI;
    c = SYSTEM_BIG;
    i = SYSTEM_WIDE;
    b = SYSTEM_PI;
    take(SYSTEM_BIG);
    char d = SYSTEM_BIG;
    char e[2] = { SYSTEM_BIG, 1 };
    SYSTEM_SET(c, 1000);
    SYSTEM_SET(i, 2.5);
    SYSTEM_CALL(take, 1001);
    SYSTEM_DECLARE(f, 1002);
    USER_SET(c, 1003);
    USER_SET(i, 3.5);
    c = flag ? SYSTEM_BIG : 1;
    if (flag)
        SYSTEM_RETURN(1004);
    return (char)(d + e[0] + f + b);
}
enum Color { RED };
enum Shape { SQUARE };
# 1 "system-enumerations.h" 3
#define SYSTEM_SQUARE SQUARE
#define SYSTEM_ASSIGN(x, v) ((x) = (v))
# 40 "conversions.c"
enum Color paint(enum Color c)
{
    c = SYSTEM_SQUARE;
    SYSTEM_ASSIGN(c, SQUARE);
    return c;
}
