// A block comment is one space, so a newline inside one does not end a
// macro definition: CPython's FutureObj_HEAD writes a comment spanning
// lines between its spliced ones, and the bit-fields after it vanished
// from the struct.  Both trailing pasted-name bit-fields are read back.
#define HEAD(prefix)                    \
    void *prefix##_loop;                \
    int prefix##_state;                 \
    /* trailing comment
       spanning lines
    */                                  \
    unsigned prefix##_log_tb: 1;        \
    unsigned prefix##_blocking: 1;

typedef struct {
    HEAD(fut)
} FutureObj;

int main(void)
{
    FutureObj fut;
    fut.fut_log_tb = 1;
    fut.fut_blocking = 0;
    return fut.fut_log_tb == 1 && fut.fut_blocking == 0 ? 0 : 1;
}
