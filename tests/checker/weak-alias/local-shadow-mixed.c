long alias(int);
int target = 1;
#pragma weak alias = target
int use(int alias)
{
    int result = alias;
    { int alias = 3; result += alias; }
    for (int alias = 0; alias < 2; ++alias) { result += alias; }
    return result;
}
