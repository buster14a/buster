// A static object may initialize its struct's flexible array member: GCC
// and Clang size the object past the struct by the elements written, and
// CPython's empty PyDictKeysObject is the shape -- eight DKIX_EMPTY entries
// in a `char dk_indices[]` tail. The tail's last element is read back, so
// an object sized without the extension returns garbage or faults rather
// than passing.
struct keys
{
    long refcnt;
    unsigned char log2_size;
    char indices[];
};
static struct keys empty_keys = {
    9,
    3,
    {-1, -1, -1, -1, -1, -1, -1, -1},
};
int main(void)
{
    return empty_keys.refcnt == 9 && empty_keys.indices[7] == (char)-1 ? 0 : 1;
}
