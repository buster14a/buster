// test_differential run-identity control. Rows that produce byte-identical
// artifacts share one link/run observation, so a candidate run must not be
// able to observe which matrix row compiled it. Row directories are named
// <allocator>_<optimization>_p<bits> and always contain '_'; shared artifact
// runs and the independent host-o0/host-o2 references use directories without
// it. Exit 0 when the executable's immediate parent directory has no '_'.
int main(int argc, char** argv)
{
    int result = argc < 1 || !argv[0];
    if (!result)
    {
        char const* name = argv[0];
        unsigned long length = 0;
        unsigned long parent_end = 0;
        unsigned long parent_start = 0;
        while (name[length]) { length += 1; }
        for (unsigned long index = 0; index < length; index += 1)
        {
            if (name[index] == '/' || name[index] == '\\')
            {
                parent_start = parent_end;
                parent_end = index + 1;
            }
        }
        for (unsigned long index = parent_start; index + 1 < parent_end; index += 1)
        {
            result |= name[index] == '_';
        }
    }
    return result;
}
