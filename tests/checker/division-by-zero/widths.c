void more(_Atomic int a, _Bool b, _BitInt(135) w, _BitInt(8) i)
{
    a /= 0;
    b %= 0;
    w /= 0;
    i %= 0;
    w /= ((unsigned _BitInt(135))1 << 130);
    i /= ((unsigned _BitInt(135))1 << 130);
}
