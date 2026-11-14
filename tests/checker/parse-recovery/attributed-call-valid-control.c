int known(int value)
{
    return value + 1;
}

int check(void)
{
    __attribute__((unused)) int value = 3;
    __attribute__((unused)) int local(int);
    return known(4);
}
