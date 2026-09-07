int puts(const char*);
int atexit(void (*function)(void));
_Noreturn void exit(int);
void shared_work(void);

static void user_handler(void)
{
    puts("user handler");
}

#ifdef EXECUTABLE_LIFECYCLE
__attribute__((constructor)) static void executable_init(void)
{
    puts("executable init");
}

__attribute__((destructor)) static void executable_fini(void)
{
    puts("executable fini");
}
#endif

int main(int argc, char** argv)
{
    int failed = argc < 1 || argv == 0 || argv[0] == 0;
    failed = failed || atexit(user_handler) != 0;
    shared_work();
    if (argc > 1)
    {
        exit(failed);
    }
    return failed;
}
