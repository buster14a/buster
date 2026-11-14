typedef struct { char* pointer; unsigned long length; } S8;
#define T(text) ((S8){ .pointer = (char*)(text), .length = sizeof(text) - 1, })
static const S8 file_scope[] = { T(" "), T("/**/") };
int f(void)
{
    static const S8 gaps[] = { T(" "), T("/**/"), T(" \\\n") };
    _Static_assert(sizeof gaps == 3 * sizeof(S8), "three");
    static const S8 one = (S8){ "a", 1 };
    return (int)(gaps[1].length + file_scope[1].length + one.length);
}
