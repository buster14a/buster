_Static_assert(_Generic(0wb, _BitInt(2): 1, default: 0) && _Generic(1wb, _BitInt(2): 1, default: 0) && _Generic(2WB, _BitInt(3): 1, default: 0), "s");
_Static_assert(_Generic(0uwb, unsigned _BitInt(1): 1, default: 0) && _Generic(5Uwb, unsigned _BitInt(3): 1, default: 0), "u");
_Static_assert(_Generic(5wbU, unsigned _BitInt(3): 1, default: 0) && _Generic(5WBu, unsigned _BitInt(3): 1, default: 0) && 5uWB == 5, "o");
_Static_assert(_Generic(0777wb, _BitInt(10): 1, default: 0) && _Generic(0b1010'1010wb, _BitInt(9): 1, default: 0) && 0777wb == 511, "r");
_Static_assert(_Generic(0x8000'0000'0000'0000wb, _BitInt(65): 1, default: 0) && _Generic(0xffffffffffffffffuwb, unsigned _BitInt(64): 1, default: 0), "x");
_Static_assert(_Generic(18446744073709551616wb, _BitInt(66): 1, default: 0) && 18446744073709551616wb == (_BitInt(66))1 << 64, "w");
_Static_assert(_Generic(0xffffffffffffffffffffffffffffffffuwb, unsigned _BitInt(128): 1, default: 0) &&
               0xffffffffffffffffffffffffffffffffuwb == (unsigned _BitInt(128))-1, "m");
_Static_assert(_Generic(0x7fffffffffffffffffffffffffffffffwb, _BitInt(128): 1, default: 0) && -1wb < 0 && _Generic(-1wb, _BitInt(2): 1, default: 0), "n");
unsigned _BitInt(100) a = 0xf'ffff'ffff'ffff'ffff'ffff'ffffuwb;
int f(void) { return (int)(a >> 90) + (int)0x1ffffffffffffffffuwb; }
