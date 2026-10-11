// The dylib side of tools/test_mach_o_dylib_data_lld.sh: data and a function
// that only a dynamic library defines (#1454).
int mach_dylib_data = 42;

int mach_dylib_function(int value)
{
    return value + 1;
}
