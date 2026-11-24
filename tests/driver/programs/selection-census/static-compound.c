typedef struct { char* pointer; unsigned long length; } S8;
int f(void)
{
    static const S8 gaps[] = { (S8){ "a", 1 }, (S8){ "bc", 2 } };
    S8 local = (S8){ "d", 1 };
    return (int)(gaps[1].length + local.length);
}
