// `__typeof__` over a comma expression's subscript -- Py_XSETREF through
// _PyTuple_ITEMS expands to `((void)0, (cast)->ob_item)[0]` -- resolves
// through the last comma operand, and the base's own parentheses only strip
// when they wrap the whole range, or `(cast)(operand)` peels into soup.
typedef struct tuple { long refs; void* ob_item[4]; } PyTupleObject;
void set_slot(void* args, void* result)
{
    __typeof__(((void)0, (((void)(0), ((PyTupleObject*)(args)))->ob_item))[0])* _tmp_dst_ptr =
        &(((void)0, (((void)(0), ((PyTupleObject*)(args)))->ob_item))[0]);
    *_tmp_dst_ptr = result;
}
int main(void)
{
    PyTupleObject t = {1, {0, 0, 0, 0}};
    int marker;
    set_slot(&t, &marker);
    return t.ob_item[0] == (void*)&marker ? 0 : 1;
}
