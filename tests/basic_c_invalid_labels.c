static int computed_goto_overwritten_zero(int selector)
{
    void *targets[2];
    targets[0] = &&zero;
    targets[1] = &&one;
    targets[0] = 0;
    goto *targets[selector & 1];
zero:
    return 37;
one:
    return 41;
}

int invalid_unsafe_branch(void)
{
    void *target = 0;
    goto *target;
}

int main(void)
{
    return computed_goto_overwritten_zero(0);
}
