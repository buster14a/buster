static const unsigned _BitInt(300) bad = ((unsigned _BitInt(300))1 << 250) + 1;
unsigned _BitInt(300) g(unsigned _BitInt(300) x)
{
return __builtin_align_down(x, bad);
}
