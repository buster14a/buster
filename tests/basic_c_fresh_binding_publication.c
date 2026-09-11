int binding_callee(void)
{
    return 7;
}

int binding_first(void)
{
    extern int binding_fresh_0(void);
    extern int binding_fresh_1(void);
    extern int binding_fresh_2(void);
    extern int binding_fresh_3(void);
    extern int binding_fresh_4(void);
    extern int binding_fresh_5(void);
    extern int binding_fresh_6(void);
    extern int binding_fresh_7(void);
    int failed = 0;
    int binding_callee = 11;
    {
        int binding_callee = 13;
        {
            extern int binding_callee(void);
            failed |= binding_callee() != 7;
        }
        failed |= binding_callee != 13;
    }
    failed |= binding_callee != 11;
    {
        extern int binding_callee(void);
        failed |= binding_callee() != 7;
    }
    return failed;
}

int binding_second(void)
{
    extern int binding_fresh_0(void);
    extern int binding_fresh_7(void);
    return binding_callee() != 7;
}

int main(void)
{
    return binding_first() | binding_second();
}
