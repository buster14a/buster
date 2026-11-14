int f(const int ci)
{
__typeof__(__builtin_align_up(ci, 4)) z = 1;
z = 2;
return z;
}
