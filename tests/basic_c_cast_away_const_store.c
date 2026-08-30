// A store through a cast that discards const:
// `((char *)formatted)[size-1] = '\0'` is how CPython's crossinterp trims
// the newline off a traceback its own accessor handed back as const.  The
// const pointer and its unqualified twin are one IR pointer type, so the
// cast is elided; the read-only mark must clear with it or the store is
// refused where the reference compilers store.
int main(void)
{
    char buffer[4] = "ab\n";
    const char* formatted = buffer;
    unsigned long size = 3;
    ((char *)formatted)[size - 1] = '\0';
    return buffer[2] == '\0' ? 0 : 1;
}
