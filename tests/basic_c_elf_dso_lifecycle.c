int puts(const char*);

__attribute__((constructor)) static void shared_init(void)
{
    puts("shared init");
}

__attribute__((destructor)) static void shared_fini(void)
{
    puts("shared fini");
}

void shared_work(void)
{
    puts("shared work");
}
