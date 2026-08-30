// Py_CLEAR takes `_Py_TYPEOF(op)* _tmp_op_ptr = &(op);` and _testcapimodule
// hands it `*p++`: an increment keeps its operand's type -- no integer
// promotion, pointers allowed -- and a trailing `++` is necessarily a
// top-level postfix operator, so `__typeof__` over both spellings resolves
// or the declaration it types declares nothing.
int main(void)
{
    int values[2] = {7, 9};
    int* p = values;
    do
    {
        __typeof__(*p++)* tmp_ptr = &(*p++);
        __typeof__(*p++) old = (*tmp_ptr);
        if (old != 7)
        {
            return 1;
        }
        if (tmp_ptr != &values[0])
        {
            return 2;
        }
    } while (0);
    if (p != values + 1)
    {
        return 3;
    }
    __typeof__(++p) q = p;
    if (q != values + 1)
    {
        return 4;
    }
    __typeof__(p--) r = q;
    if (*r != 9)
    {
        return 5;
    }
    char narrow = 3;
    if (sizeof(++narrow) != 1)
    {
        return 6;
    }
    return 0;
}
