// Declarators that return function pointers, and the type names that cast to
// them.  SQLite's VFS layer is written in this grammar: `xDlSym` is declared
// as `void (*(*xDlSym)(sqlite3_vfs *, void *, const char *))(void)`, the Unix
// VFS holds dlsym in a local of that shape and casts to the matching abstract
// declarator, and `sqlite3_module.xFindFunction` takes a function-pointer
// parameter that has parameters of its own.
typedef unsigned long size_type;

static int add(int a, int b) { return a + b; }
static int multiply(int a, int b) { return a * b; }

static void mark(void) {}

// A function whose declarator returns a pointer to a function.
static int (*pick(int which, const char *tag))(int, int)
{
    (void)tag;
    return which ? add : multiply;
}

// The same shape with a variadic parameter list of its own.
static int (*pick_variadic(int which, ...))(int, int) { return which ? add : multiply; }

// A redundantly parenthesized function definition still declares a function.
static int (doubled)(int value) { return value * 2; }

// A member two declarator groups deep, and one whose parameter is itself a
// function pointer with parameters: the enclosing function type must claim
// three parameters, not five.
struct Module
{
    void (*(*resolve)(void *handle, const char *symbol))(void);
    int (*find_function)(void *table, int argument_count, void (**function)(void *, int));
};

static void (*resolve_symbol(void *handle, const char *symbol))(void)
{
    (void)handle;
    (void)symbol;
    return mark;
}

static void found(void *context, int value)
{
    (void)context;
    (void)value;
}

static int find_function(void *table, int argument_count, void (**function)(void *, int))
{
    (void)table;
    *function = found;
    return argument_count;
}

static struct Module module = {resolve_symbol, find_function};

int main(void)
{
    int (*chosen)(int, int) = pick(1, "add");
    int (*other)(int, int) = pick(0, "multiply");
    int (*variadic)(int, int) = pick_variadic(1);
    void (*symbol)(void);
    void (*through_cast)(void);
    void (*(*resolver)(void *, const char *))(void);
    int (*table[2])(int, int) = {add, multiply};
    int (**indirect)(int, int) = table;
    void (*callback)(void *, int) = 0;

    if (chosen(2, 3) != 5) return 1;
    if (other(2, 3) != 6) return 2;
    if (variadic(4, 5) != 9) return 3;
    if (doubled(21) != 42) return 4;
    if (table[0](1, 1) != 2 || table[1](3, 3) != 9) return 5;
    // A cast to a pointer to a function pointer: the stars are an abstract
    // declarator, not a call of the parameter list that follows them.
    if ((*(int (**)(int, int))indirect)(2, 2) != 4) return 6;

    symbol = module.resolve(0, "mark");
    if (symbol != mark) return 7;

    resolver = (void (*(*)(void *, const char *))(void))resolve_symbol;
    through_cast = (*resolver)(0, "mark");
    if (through_cast != mark) return 8;

    if (module.find_function(0, 3, &callback) != 3) return 9;
    if (callback != found) return 10;

    if (sizeof(void (*(*)(void *, const char *))(void)) != sizeof(void *)) return 11;
    if (sizeof(int (**)(int, int)) != sizeof(void *)) return 12;
    (void)(size_type)0;
    return 0;
}
