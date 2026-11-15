#define SPECIFIERS _Atomic __auto_type
#define DECLARATION _Atomic __auto_type *generated = &object
#define DECLARE(name, expression) _Atomic __auto_type *name = expression

int object;
SPECIFIERS *named = &object;
DECLARATION;
DECLARE(argument, &object);
