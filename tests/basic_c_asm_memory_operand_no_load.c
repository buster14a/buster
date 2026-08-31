// An "m" operand names a place; the template decides whether to read it.
// mimalloc's thread-slot primitive hands `*((void**)0)` to a %fs-override
// read -- %fs:0 is the thread pointer -- so a pre-load of the operand
// faults on address zero before the assembly ever runs.  The recovered
// place must still reach the template: the returned thread pointer is
// checked as nonzero, which only the fs-relative read can produce.  The
// puts call keeps the image hosted -- the freestanding stub installs no
// thread pointer, and %fs there faults legitimately -- and its prototype is
// written out rather than included, because these driver invocations name
// no sysroot.  Linux x86-64 only: %fs:0 is glibc's thread pointer.
int puts(const char* text);

static void* basic_asm_read_thread_pointer(void)
{
    void* result;
    unsigned long offset = 0;
    __asm__("movq %%fs:%1, %0" : "=r"(result) : "m"(*((void**)offset)));
    return result;
}

int main(void)
{
    if (basic_asm_read_thread_pointer() == 0)
    {
        return 1;
    }
    return puts("memory operand ok") < 0;
}
