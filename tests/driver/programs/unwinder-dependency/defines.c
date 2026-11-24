int helper(void);
int _Unwind_Backtrace(void* trace, void* argument)
{
    (void)trace;
    *(int*)argument = 7;
    return 0;
}
int main(void) { return helper(); }
