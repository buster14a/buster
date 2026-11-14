int f(int x, char* p)
{
return __builtin_align_up(x, 2147483648L) + (int)(long)__builtin_align_down(p, 0x8000000000000000ul);
}
