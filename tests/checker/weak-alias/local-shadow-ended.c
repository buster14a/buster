extern long alias;
int target = 1;
#pragma weak alias = target
int use(void)
{
    { int alias = 3; (void)alias; }
    return alias;
}
