extern int target;
int target = 1;
extern int alternate __attribute__((alias("target")));
int loader __attribute__((loader_uninitialized));
static int internal;
extern int internal;
int read_internal(void)
{
    return internal;
}
