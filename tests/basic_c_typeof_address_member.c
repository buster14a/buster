// `__typeof__` over a member reached through an address-of base:
// `(&(interp)->xi)->PyExc_NotShareableError` is CPython's Py_CLEAR shape in
// crossinterp.c.  The base recurses through the same walker, whose prefix
// handling wraps the pointer, and the member chain continues off it.
typedef struct exceptions { void* PyExc_NotShareableError; } exceptions;
typedef struct interp_state { int id; exceptions xi; } interp_state;
void clear_error(interp_state* interp)
{
    __typeof__((&(interp)->xi)->PyExc_NotShareableError)* _tmp_op_ptr = &((&(interp)->xi)->PyExc_NotShareableError);
    __typeof__((&(interp)->xi)->PyExc_NotShareableError) _tmp_old_op = (*_tmp_op_ptr);
    if (_tmp_old_op != 0) { *_tmp_op_ptr = 0; }
}
int main(void)
{
    interp_state s;
    s.xi.PyExc_NotShareableError = &s;
    clear_error(&s);
    return s.xi.PyExc_NotShareableError == 0 ? 0 : 1;
}
