int f(int x)
{
const int z = 0;
return __builtin_align_down(x, z);
}
