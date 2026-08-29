// Weak references in a hosted program, which is where the answer stops being
// the linker's alone. tests/basic_c_global_asm_weak.c is the static half of
// this: there nothing else is undefined, so the image has no loader and every
// weak reference is worth zero. Here a shared library is present, and the
// three references below want three different things from it.
//
// `puts` is declared weak and libc defines it: the reference must still reach
// the library, which is what an image that resolved every weak reference to
// zero at link time would have broken.
//
// The two symbols nothing in the link defines are referenced but not
// inspected. A default-visibility weak reference is preemptible by definition
// -- the loader is the only party that could still find one -- so buster
// leaves it in .dynsym for the loader to answer, as STB_WEAK so that finding
// nothing is an answer rather than a refused image. Taking their addresses is
// what makes the image carry them; reaching main at all is the check, because
// before the weak binding reached .dynsym the loader rejected the program.
// What the addresses come out as is deliberately not checked: the linker
// cannot yet tell a weak reference libc happens to define from one nothing
// does -- the shared library read it has records only the defined objects a
// copy relocation needs -- so the second still gets a PLT thunk or a copy
// slot rather than zero. That is issue #656.
//
// The hidden weak reference is the one this file still pins down exactly. It
// cannot be preempted, so a dynamic image owes it the same zero a static one
// does, and the assembly block is the only way to spell hidden today.

extern __attribute__((weak)) int puts(char const* text);

extern __attribute__((weak)) unsigned long weak_undefined_object;
__attribute__((weak)) void weak_undefined_function(void);

// Written rather than read: the stores are what keep the two references in
// the image.
void* weak_undefined_addresses[2];

#if defined(__x86_64__) || defined(_M_X64)

unsigned long* weak_undefined_hidden_address(void);

__asm__(".text\n"
        ".globl weak_undefined_hidden_address\n"
        ".type weak_undefined_hidden_address, @function\n"
        "weak_undefined_hidden_address:\n"
        ".weak weak_undefined_hidden\n"
        ".hidden weak_undefined_hidden\n"
        "	lea weak_undefined_hidden(%rip), %rax\n"
        "	ret\n");

#endif

int main(void)
{
    int failures = 0;
    weak_undefined_addresses[0] = &weak_undefined_object;
    weak_undefined_addresses[1] = (void*)weak_undefined_function;
    failures += puts == 0 ? 1 : 0;
    failures += puts != 0 && puts("weak reference bound to the library") < 0 ? 1 : 0;
#if defined(__x86_64__) || defined(_M_X64)
    failures += weak_undefined_hidden_address() == 0 ? 0 : 1;
#endif
    return failures;
}
