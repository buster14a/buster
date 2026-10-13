typedef void (*callback)(void);
static int hits;
__attribute__((noreturn)) static void never(void)
{
    for (;;) {}
}

void setter(__attribute__((noreturn)) callback routine)
{
    (void)routine;
    hits += 1;
}

void pointer_setter(__attribute__((noreturn)) void (*routine)(void))
{
    (void)routine;
    hits += 2;
}

void (grouped_setter)(__attribute__((noreturn)) callback routine)
{
    (void)routine;
    hits += 4;
}

__attribute__((noreturn)) void true_prefix(callback routine)
{
    routine();
    for (;;) {}
}

void true_suffix(callback routine) __attribute__((noreturn));
void true_suffix(callback routine)
{
    routine();
    for (;;) {}
}

void true_mixed(__attribute__((noreturn)) callback routine) __attribute__((noreturn));
void true_mixed(__attribute__((noreturn)) callback routine)
{
    routine();
    for (;;) {}
}

int through_true_mixed(void)
{
    true_mixed(never);
    return 1;
}

int through_true_prefix(void)
{
    true_prefix(never);
    return 1;
}

int through_true_suffix(void)
{
    true_suffix(never);
    return 1;
}

int main(void)
{
    extern void setter(__attribute__((noreturn)) callback routine);
    setter(never);
    pointer_setter(never);
    grouped_setter(never);
    return hits != 7;
}
