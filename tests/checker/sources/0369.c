typedef struct { char* pointer; unsigned long length; } S8;
struct W { S8 s; S8 t[2]; };
int f(int n)
{
    static const S8 g[] = { (S8){ "a", n } };
    return (int)sizeof g;
}
