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

void attribute_noreturn(int status) { exit(status); }
void specifier_noreturn(int status) { exit(status); }

static int through_attribute(int status) { attribute_noreturn(status); }

static int through_specifier(int status) { specifier_noreturn(status); }

static int through_library(int status) { exit(status); }

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
    // four bodies lower at all, and that the process still exits with 0.
    if (argc > 3) return through_attribute(1);
    if (argc > 2) return through_specifier(2);
    if (argc > 1) return through_library(3);
    return guarded(1) == 6 ? 0 : 4;
}
