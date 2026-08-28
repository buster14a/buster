// Declared rather than included: the driver test compiles this without an
// -isysroot.  printf and puts are two different stdio entry points onto the
// same buffered stream, which is what this needs -- `stdout` itself cannot be
// declared here without a FILE definition.
extern int printf(const char *format, ...);
extern int puts(const char *text);

// C 5.1.2.2.3: returning from `main` is equivalent to calling `exit` with that
// value, so the C runtime's exit processing has to happen -- and its most
// visible part is flushing every buffered stream.  A synthesized entry point
// that ends in the raw exit_group syscall skips all of it, and a program that
// only printed and returned produces no output whatsoever.  Upstream LZ4's
// tests/datagen writes its generated corpus to stdout and returns from main,
// so the whole harness read an empty corpus.
//
// Nothing here calls fflush, and that is the entire point of the fixture: the
// driver test spawns it with stdout captured -- a pipe, so the stream is fully
// buffered -- and compares the bytes.  Keep the two writes on different stdio
// entry points so a flush that only covers one of them still fails.  puts
// supplies the trailing newline itself, so the expected bytes are unchanged.
int main(void)
{
    printf("buffered stdout survives returning from main\n");
    puts("and so does a second buffered write");
    return 0;
}
