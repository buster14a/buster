// A Buster-compiled object whose references to dylib data a platform linker
// must bind through the GOT (#1454): loads, address-taking and calls of
// symbols a dynamic library defines, beside local and global definitions.
extern int mach_dylib_data;
extern int mach_dylib_function(int value);

static int mach_local_data = 5;
int mach_global_data = 7;

int mach_dylib_data_sum(void)
{
    return mach_dylib_data + mach_local_data + mach_global_data + mach_dylib_function(3);
}

int* mach_dylib_data_address(void)
{
    return &mach_dylib_data;
}

int (*mach_dylib_function_address(void))(int)
{
    return mach_dylib_function;
}

int main(void)
{
    return mach_dylib_data_sum() == 58 && *mach_dylib_data_address() == 42 && mach_dylib_function_address()(1) == 2 ? 0 : 1;
}
