#include <stdio.h>

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
// entry points so a flush that only covers one of them still fails.
int main(void)
{
    printf("buffered stdout survives returning from main\n");
    fputs("and so does a second buffered write\n", stdout);
    return 0;
}
