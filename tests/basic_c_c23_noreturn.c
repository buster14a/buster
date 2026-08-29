// The one C23 attribute buster acts on.  basic_c_c23_attributes.c proves the
// rest are parsed and discarded; this one proves [[noreturn]] is read, and it
// is written as a pair so the proof discriminates: die_marked and die_plain
// differ in nothing but the attribute, so a compiler that ignored it would
// lower the two callers identically.
//
// c_ir_noreturn_marker_in_range has had a [[ branch since it was written, but
// it was unreachable dead code -- an attributed declaration failed to parse
// long before the lowering could ask about it -- so the driver test checks the
// generated assembly rather than only that this runs: a call to a noreturn
// callee ends control flow, and the caller's fallthrough is replaced by a trap
// instead of an ordinary return sequence.
//
// Declared rather than included: the driver test compiles this without an
// -isysroot, the same way basic_c_noreturn_call.c declares exit.
__attribute__((noreturn)) void exit(int status);

// The marked callee.  Both the plain C23 spelling and the reserved scoped one
// have to be read; glibc-style headers use the reserved spellings so a user
// macro named `noreturn` cannot capture them.
[[noreturn]] void die_marked(int status);
[[__gnu__::__noreturn__]] void die_scoped(int status);

// The unmarked control.  Identical in every other respect.
void die_plain(int status);

void die_marked(int status) { exit(status); }
void die_scoped(int status) { exit(status); }
void die_plain(int status) { exit(status); }

// A non-void function whose body ends in a noreturn call has not fallen off
// its end, so these need no return statement.  through_plain does, which is
// exactly the difference the assembly assertion looks for.
int through_marked(int status) { die_marked(status); }

int through_scoped(int status) { die_scoped(status); }

int through_plain(int status)
{
    die_plain(status);
    return 0;
}

// The marker also has to survive on a definition rather than only on a
// declaration, which is the shape the issue reported failing with an
// "undeclared identifier" on the parameter itself.
[[noreturn]] void die_defined(int status)
{
    exit(status);
}

int main(int argc, char** argv)
{
    (void)argv;
    // Only the last of these ever runs.  The point of the fixture is that all
    // four bodies lower at all and that the process still exits with 0.
    if (argc > 4)
    {
        return through_marked(1);
    }
    if (argc > 3)
    {
        return through_scoped(2);
    }
    if (argc > 2)
    {
        return through_plain(3);
    }
    if (argc > 1)
    {
        die_defined(4);
    }
    return 0;
}
