// A lone identifier a block assertion's scope binds to an object is that
// object, whatever typedef shares the spelling further out: ctypes declares
// `typedef PyObject *string;` at file scope and asserts over a local
// `char string[256]`.  The type interpretation must lose to the shadow.
typedef void* string;
int probe(void)
{
    char string[256];
    (void)string;
    _Static_assert(sizeof(string) == 256, "local wins over typedef");
    return 0;
}
int main(void) { return probe(); }
