// The three ELF thread-local models a reference can be lowered to, and the
// one property they share: whatever sequence answers `&x`, it answers with
// the address of this thread's `x` and disturbs nothing around it.
//
// Which model a reference gets is settled at compile time and cannot be seen
// from inside the program (issue #751):
//
//   local-exec      a definition in this module, built for an executable.  A
//                   constant offset from the thread pointer, folded into a
//                   lea.
//   initial-exec    a declaration this module does not define, built for an
//                   executable.  The offset is a GOT word the loader fills,
//                   so the reference costs a load; it is what makes a
//                   thread-local defined by a shared library reachable at all.
//   general-dynamic -fPIC.  A call to __tls_get_addr, because an object that
//                   may end up in a shared library that may be dlopened has
//                   no offset from the thread pointer until it is loaded.
//
// This file is compiled both ways, so a plain build takes the first two and a
// -fPIC build takes the third for every reference in it.  What that costs the
// register allocators is the point of the shape below: general-dynamic is a
// call, it writes RDI and returns in RAX, and the values live across it here
// -- `carried`, the accumulator, the pointer taken before it -- have to
// survive it.  An allocator that treats the sequence as a plain lea loses
// them.  Every check exits non-zero with its own number, so a failure names
// itself.

__thread int local_exec_value = 7;
__thread long long local_exec_wide = 8;
__thread char local_exec_zero;

extern __thread int initial_exec_value;
extern __thread long long initial_exec_wide;

int thread_local_models_add(int left, int right);

static int accumulate(void)
{
    // Four values live at once across three thread-local accesses, with a
    // call between two of them: this is the register pressure a
    // general-dynamic sequence has to be modelled through.
    int carried = 3;
    int *pointer = &local_exec_value;
    int accumulator = initial_exec_value;
    accumulator += *pointer;
    accumulator = thread_local_models_add(accumulator, carried);
    accumulator += initial_exec_value;
    return accumulator + carried + *pointer;
}

int main(void)
{
    if (local_exec_value != 7)
    {
        return 1;
    }
    if (local_exec_wide != 8)
    {
        return 2;
    }
    if (local_exec_zero != 0)
    {
        return 3;
    }
    if (initial_exec_value != 11)
    {
        return 4;
    }
    if (initial_exec_wide != 12)
    {
        return 5;
    }
    // 11 + 7 + 3 + 11 + 3 + 7
    if (accumulate() != 42)
    {
        return 6;
    }
    local_exec_value += 1;
    initial_exec_value += 1;
    local_exec_zero += 5;
    if (local_exec_value != 8 || initial_exec_value != 12 || local_exec_zero != 5)
    {
        return 7;
    }
    if (&local_exec_value == (int *)&initial_exec_value)
    {
        return 8;
    }
    if (*&local_exec_wide != 8 || *&initial_exec_wide != 12)
    {
        return 9;
    }
    return 0;
}
