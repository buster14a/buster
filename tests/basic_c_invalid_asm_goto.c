int main(void)
{
    __asm__ goto("j %l1" ::: : target);
    return 0;
target:
    return 0;
}
