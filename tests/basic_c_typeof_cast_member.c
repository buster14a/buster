// `__typeof__` over a member read through a cast base:
// `_Py_TYPEOF(dst)* _tmp_dst_ptr = &(dst);` is CPython's Py_SETREF, and its
// callers write dst as `((propertyobject *) new)->prop_name`.  The
// expression-type walker required the postfix chain's base to be a single
// identifier, so the declaration it typed declared nothing and every use of
// the temporary was an undeclared identifier.  The swap's answers are
// checked so a base typed off the wrong half returns the wrong pointer.
typedef struct property
{
    int refs;
    char* prop_name;
} propertyobject;

static char* swap(void* new_object, char* value)
{
    __typeof__(((propertyobject *) new_object)->prop_name)* _tmp_dst_ptr = &(((propertyobject *) new_object)->prop_name);
    __typeof__(((propertyobject *) new_object)->prop_name) _tmp_old_dst = (*_tmp_dst_ptr);
    *_tmp_dst_ptr = value;
    return _tmp_old_dst;
}

int main(void)
{
    propertyobject object = {1, 0};
    char storage;
    char* old = swap(&object, &storage);
    if (old != 0)
    {
        return 1;
    }
    if (object.prop_name != &storage)
    {
        return 2;
    }
    return 0;
}
