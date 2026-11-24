extern void __splitstack_getcontext(void* context[10]);
int main(void)
{
    void* context[10];
    __splitstack_getcontext(context);
    return 0;
}
