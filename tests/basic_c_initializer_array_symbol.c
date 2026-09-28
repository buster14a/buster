// A variable placed in `.init_array` or `.fini_array` is both an entry the
// startup calls and an ordinary object the program can read (issue 1728).
// The entry-stub writers call the entries directly instead of walking the
// arrays, so the arrays' bytes still have to be laid out wherever a symbol is
// defined inside them: a read through `early_entry` or `late_entry` must see
// the function it holds, as it does in an image GNU ld links.  The driver
// tests compile this with a host compiler, so the linker reads a foreign object.
//
// `main` computes the verdict and `late` reports it, so the status also proves
// that the `.fini_array` entry ran; a status nothing overwrote is main's 1.

extern void _exit(int status);

static int sequence;
static int verdict = 1;

static void early(void)
{
    sequence = sequence * 10 + 2;
}

static void late(void)
{
    _exit(verdict);
}

__attribute__((constructor(101))) static void first(void)
{
    sequence = sequence * 10 + 1;
}

__attribute__((section(".init_array"), used)) void (*early_entry)(void) = early;
__attribute__((section(".fini_array"), used)) void (*late_entry)(void) = late;

int main(void)
{
    verdict = sequence != 12 ? 2 : early_entry != early ? 3 : late_entry != late ? 4 : 0;
    return 1;
}
