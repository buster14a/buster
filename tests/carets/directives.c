// The preprocessor's diagnostics, each a caret alone: at a line's start, in its middle and at its end.
#warning at the start of a line
    #warning indented by spaces
#define REDEFINED 1
#define REDEFINED 2
#if 08
#endif
#pragma GCC warning "a pragma's warning"
int multiple = 'ab';
int call(void) { return undeclared(); }
#error at the end of the file
