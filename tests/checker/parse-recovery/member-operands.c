// Semantic actions in the statement that a syntax error interrupts (#1249).
#define BOUND_NAME missing_bound
enum { count = 3 };
struct Valid { unsigned char data[count]; unsigned : count; };

void statements(int visible)
{
    undeclared_x foo;
    (visible + missing_operand) unread_tail;
}

struct Array { unsigned char [BOUND_NAME 4]; };
struct Width { unsigned : x 5; };
struct Grouped { unsigned : (count + missing_width) 5; };
struct KnownBound { unsigned char [count 4]; };

void local(int parameter)
{
    enum { local_width = 2 };
    struct Local { unsigned : local_width 5; };
    struct Nonconstant { unsigned : (1 + (parameter * 2)) 5; };
    struct Hidden { unsigned : later_width 5; };
    enum { later_width = 1 };
    (void)after_local;
}

struct BadLiteral { unsigned : (0xz + 1) 5; };
struct TypedBound { int [1.5 4]; };

int returned(void)
{
    return (missing_return + 1) unread_tail;
}

struct Partial { unsigned : missing_before_error 5; int broken = 3; };
