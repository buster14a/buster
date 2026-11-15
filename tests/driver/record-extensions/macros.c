#define ZERO 0
#define WRAP(value) (value)
#define ARRAY(name, bound) int name[bound]
#define EMPTY(name) struct name {}
ARRAY(first, ZERO);
ARRAY(second, WRAP(0));
ARRAY(literal, 0);
ARRAY(grouped, (0));
ARRAY(sum, 0 + 0);
EMPTY(Empty);
struct Outer { EMPTY(Inner) member; };
#define TYPE(bound) __typeof__(int[bound])
#define INFER(name, value) int name[] = value
TYPE(ZERO) type_single;
TYPE(WRAP(0)) type_multiple;
INFER(inferred, {});
