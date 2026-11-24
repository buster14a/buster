enum U { UA = 1 };
unsigned f(enum U e)
{
return __builtin_stdc_leading_zeros(e);
}
