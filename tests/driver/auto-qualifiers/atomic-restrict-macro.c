#define SPECIFIERS restrict _Atomic __auto_type
#define DECLARATION restrict _Atomic __auto_type generated = 1
#define DECLARE(name) restrict _Atomic __auto_type name = 2

SPECIFIERS named = 0;
DECLARATION;
DECLARE(argument);
