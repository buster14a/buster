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
// The two symbols nothing in the link defines are worth zero, and are read
// here rather than merely referenced. A default-visibility weak reference is
// preemptible, so the loader would be the party to answer it -- but only if
// some library the image names actually defines it, and asking the loader for
// a name nothing has is how these two used to come back as their own PLT
// thunk or copy slot (issue #656). The linker now reads what each library
// exports, finds neither name, and relocates both against zero, which is what
// `ld` does with them and what the program asks for by declaring them weak.
//
// The hidden weak reference is the one that never depended on any of that. It
// cannot be preempted, so a dynamic image owes it the same zero a static one
// does, and the assembly block is the only way to spell hidden today.

extern __attribute__((weak)) int puts(char const* text);

extern __attribute__((weak)) unsigned long weak_undefined_object;
__attribute__((weak)) void weak_undefined_function(void);

// Written as well as read: the stores are a second way of keeping the two
// references in the image, and they put the addresses somewhere the compiler
// cannot fold the comparisons below through.
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
    failures += weak_undefined_addresses[0] == 0 ? 0 : 1;
    failures += weak_undefined_addresses[1] == 0 ? 0 : 1;
#if defined(__x86_64__) || defined(_M_X64)
    failures += weak_undefined_hidden_address() == 0 ? 0 : 1;
#endif
    return failures;
}
