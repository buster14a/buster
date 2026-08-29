// Declared rather than included: the driver test compiles this without an
// -isysroot.  The attribute is the point -- it is how glibc spells exit, and
// what the flow analysis has to read -- so declaring it here tests the same
// mechanism as <stdlib.h> would have.
__attribute__((noreturn)) void exit(int status);

// A call to a noreturn callee ends control flow, so a non-void function whose
// body ends in one has not fallen off its end.  glibc marks exit and abort
// with __attribute__((__noreturn__)), and LZ4's badusage/END_PROCESS helpers
// rely on that, so the flow analysis has to read the attribute rather than a
// list of library names.
__attribute__((noreturn)) void attribute_noreturn(int status);
_Noreturn void specifier_noreturn(int status);

// A declarator list shares its specifiers, but not its declarators.  The
// attribute below is written after list_noreturn's parameter list, so it marks
// that one declarator; list_returns beside it is an ordinary function.  Reading
// the whole list instead marked both, and the block after a call to
// list_returns was terminated with a ud2 the program then executed.
void list_noreturn(int status) __attribute__((noreturn)), list_returns(int status);

// A marker in the specifiers is shared, because every declarator of the list
// is.  Both of these are noreturn.
__attribute__((noreturn)) void list_specifier_first(int status), list_specifier_second(int status);

// The marker belongs to the function, not to the declaration carrying it: a
// header may declare it plainly and a later declaration -- or the definition
// itself -- add the attribute.  A call resolves to one declaration per entity,
// so these two are noreturn only if the marker is joined across all of an
// entity's declarations.
void redeclared_noreturn(int status);
__attribute__((noreturn)) void redeclared_noreturn(int status);
void defined_noreturn(int status);
__attribute__((noreturn)) void defined_noreturn(int status) { exit(status); }

void attribute_noreturn(int status) { exit(status); }
void specifier_noreturn(int status) { exit(status); }
void list_noreturn(int status) { exit(status); }
void list_returns(int status) { (void)status; }
void list_specifier_first(int status) { exit(status); }
void list_specifier_second(int status) { exit(status); }
void redeclared_noreturn(int status) { exit(status); }

static int through_attribute(int status) { attribute_noreturn(status); }

static int through_specifier(int status) { specifier_noreturn(status); }

static int through_library(int status) { exit(status); }

static int through_list_declarator(int status) { list_noreturn(status); }

// The sibling of that declarator, in the same shape: this one falls off its
// end, so the block after its call carries the ordinary return-value store the
// driver test tells the two terminators apart by.
static int through_list_sibling(int status) { list_returns(status); }

static int through_list_specifier_first(int status) { list_specifier_first(status); }

static int through_list_specifier_second(int status) { list_specifier_second(status); }

static int through_redeclaration(int status) { redeclared_noreturn(status); }

static int through_definition(int status) { defined_noreturn(status); }

// A noreturn call inside a conditional or short-circuit operand must not end
// the block it sits in: the enclosing expression created a merge the arm still
// has to reach.  This is the shape buster's own BUSTER_CHECK expands to, and
// the reason the flow analysis has to know statement position from operand
// position.
static int guarded(int status)
{
    int value = 1;
    ((void)(status > 100 ? (attribute_noreturn(status), 0) : 0));
    value += status && (status > 100 ? (specifier_noreturn(status), 0) : 2);
    value += (status > 100 ? (exit(status), 0) : 4);
    return value;
}

int main(int argc, char **argv)
{
    (void)argv;
    // Only the last of these ever runs; the point of the fixture is that all
    // nine bodies lower at all, and that the process still exits with 0.
    if (argc > 9) return through_list_sibling(0);
    if (argc > 8) return through_list_specifier_first(1);
    if (argc > 7) return through_list_specifier_second(2);
    if (argc > 6) return through_list_declarator(3);
    if (argc > 5) return through_redeclaration(4);
    if (argc > 4) return through_definition(5);
    if (argc > 3) return through_attribute(6);
    if (argc > 2) return through_specifier(7);
    if (argc > 1) return through_library(8);
    // The declarator beside a noreturn one still returns: reaching the return
    // below is the whole assertion of the multi-declarator case, because a
    // sibling wrongly marked noreturn traps here instead.
    list_returns(argc);
    return guarded(1) == 6 ? 0 : 4;
}
