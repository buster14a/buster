typedef int myint;

void plain(void)
{
    myint & 1;
    after_plain;
}

void qualifiers(void)
{
    const myint & (1 + 2);
    after_qualifiers;
}

void preceding(void)
{
    int kept = 1;
    myint & 1;
    (void)kept;
    after_preceding;
}
