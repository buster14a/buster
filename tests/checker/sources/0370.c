typedef struct { char* pointer; unsigned long length; } S8;
struct W { S8 s; S8 t[2]; };
int f(void)
{
    static const S8* g = &(S8){ "a", 1 };
    return g != 0;
}
