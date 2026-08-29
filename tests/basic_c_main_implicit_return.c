// C 5.1.2.2.3: "reaching the } that terminates the main function returns a
// value of 0".  `main` is the only function in the language with a defined
// fall-off; every other one is undefined there (C 6.9.1p12) and this compiler
// terminates it with the IR's unreachable, which x86-64 spells `ud2` and
// AArch64 spells BRK.  `main` was getting that same terminator, so a program
// that simply ran off its last statement did all of its work correctly and
// then died on a trap: SIGILL, exit status 132.
//
// libc-test's `regression/sem_close-unmap` is nineteen lines that end with a
// bare `sem_post(sem);` before the closing brace, with no return statement
// anywhere.  It opened the semaphore twice, unlinked it, closed it once and
// posted through the mapping the second open still holds -- every one of
// those correctly -- and then faulted on the brace.
//
// This fixture is its own assertion, because the construct under test is the
// exit status itself.  Exit zero is reachable only by falling off the closing
// brace below, so a compiler that traps there faults; one that emits a bare
// `ret` without materializing the value exits with whatever the last
// computation left in the result register, which the final call deliberately
// makes 45; and one that follows the rule exits 0.  Every check that fails
// returns its own code instead, so a failure names the step it stopped at.
//
// The volatile counters are what keep any of it from folding: the calls have
// to be the ones code generation emitted, and the fall-off has to be reached
// through a loop, a switch and a nested block rather than out of the entry
// block.

static volatile int sink;
static volatile int calls;

static int identity(int value)
{
    calls += 1;
    return value;
}

static void bump(int value)
{
    calls += 1;
    sink = value;
}

int main(void)
{
    int total = 0;
    int index = 0;
    // A loop, so the closing brace is not the entry block's own fall-off.
    for (index = 0; index < 4; index += 1)
    {
        total += identity(index + 1);
    }
    if (total != 10)
    {
        return 1;
    }
    if (calls != 4)
    {
        return 2;
    }
    // A switch, whose break joins the path that reaches the brace.
    switch (identity(2))
    {
        case 2:
            sink = 1;
            break;
        default:
            return 3;
    }
    if (sink != 1)
    {
        return 4;
    }
    // A nested compound statement: its closing brace is not the function's,
    // and only the outermost one returns a value.
    {
        int local = identity(9);
        if (local != 9)
        {
            return 5;
        }
        bump(local);
    }
    if (sink != 9)
    {
        return 6;
    }
    if (calls != 7)
    {
        return 7;
    }
    // The shape the defect was found in: a call statement, then the closing
    // brace.  The call's own result is 45, so a `ret` that does not put the
    // zero in place exits 45 rather than 0.
    identity(45);
}
