typedef int v4 __attribute__((ext_vector_type(4)));

void duplicate_swizzle(v4* pointer, v4 value)
{
    pointer->xx = value.zw;
}
