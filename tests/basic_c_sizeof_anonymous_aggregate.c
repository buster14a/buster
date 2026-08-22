// sizeof over an inline struct/union/enum *definition* is valid C this
// frontend does not implement: no type-name query resolves a definition
// spelled in an expression position. The expression-type prediction used to
// fill the gap with its int guess, so
// sizeof((struct { char a; long long b; }){0}) silently compiled to 4 --
// found by tools/differential_c_harness.py, family sizeof_expr. Until inline
// definitions resolve, the honest answer is a diagnostic; the driver test
// pins the rejection so the silent misfold cannot return. When support
// lands, turn this into a run fixture asserting both values are 16.
int main(void)
{
    int type_name_form = (int)sizeof(struct { char a; long long b; });
    int literal_form = (int)sizeof((union { char a[9]; long long b; }){0});
    return type_name_form + literal_form;
}
