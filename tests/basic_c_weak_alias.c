// The shapes __attribute__((weak)) and __attribute__((alias)) come in,
// including the one musl's weak_alias() macro produces: `extern __typeof(old)
// new __attribute__((__weak__, __alias__(#old)))`. musl publishes malloc,
// free, errno and most of the pthread surface that way, so an archive built
// without the attributes compiles and does not link.
//
// A function alias, a data alias, a weak definition of each kind that is not
// an alias, and an alias whose target is static -- the case that needs the
// target kept alive, because nothing else in the unit names it.
//
// Every alias is called or read rather than only declared, because an alias
// that resolves to the wrong address still links.

int alias_target_function(int value)
{
    return value + 1;
}

static int alias_static_target(int value)
{
    return value + 2;
}

int alias_target_object = 42;

__attribute__((weak)) int weak_definition_object = 7;

__attribute__((weak)) int weak_definition_function(int value)
{
    return value + 3;
}

// The declarator spelling, which is the one that can also be called by name.
int alias_function(int value) __attribute__((__weak__, __alias__("alias_target_function")));
int alias_of_static(int value) __attribute__((__alias__("alias_static_target")));

// musl's own spelling, and the same shape through a typedef. A function
// declared through a type name rather than a parameter-list declarator has no
// function-name token, so semantic analysis takes its parameters from its type
// and files it as a function anyway (issue #641); both are called by name here
// as well as taken the address of, because a call and an address resolve
// through different paths.
extern __typeof(alias_target_function) alias_function_typeof __attribute__((__weak__, __alias__("alias_target_function")));
extern __typeof(alias_target_object) alias_object __attribute__((__alias__("alias_target_object")));
extern __typeof__(alias_target_object) alias_object_long __attribute__((alias("alias_target_object")));
typedef int AliasFunctionType(int);
extern AliasFunctionType alias_function_typedef __attribute__((__alias__("alias_target_function")));

// A type-name declaration that this unit later defines under the ordinary
// declarator spelling. The two have to stay one entity: the first is now a
// function declaration rather than an object one, and a second entity would
// make the call ambiguous or resolve it to a name nothing defines.
typedef int TypedefDeclaredFunction(int);
static TypedefDeclaredFunction typedef_declared_function;

static int typedef_declared_function(int value)
{
    return value + 4;
}

// An ordinary object whose name is one of the attribute spellings. It must
// stay a strong definition: the attribute names are looked for inside an
// attribute list, not anywhere in the declaration.
int weak = 11;
int alias = 13;

int main(void)
{
    int (*target_address)(int) = alias_target_function;
    int (*alias_address)(int) = alias_function;
    int (*typeof_alias_address)(int) = alias_function_typeof;
    int* object_target_address = &alias_target_object;
    int failures = 0;
    failures += alias_function(1) != 2;
    failures += alias_function_typeof(1) != 2;
    failures += alias_function_typedef(1) != 2;
    failures += typedef_declared_function(1) != 5;
    failures += alias_of_static(1) != 3;
    failures += alias_object != 42;
    failures += alias_object_long != 42;
    failures += weak_definition_object != 7;
    failures += weak_definition_function(1) != 4;
    failures += weak != 11;
    failures += alias != 13;
    failures += alias_address != target_address;
    failures += typeof_alias_address != target_address;
    failures += &alias_object != object_target_address;
    failures += &alias_object_long != object_target_address;
    return failures;
}
