typedef signed char S;
enum E { A = 1000, B = 2 };
int f(signed char x) { return x < 1000; }
int h(unsigned char c, S s, _Bool b, unsigned u, short sh) {
  int r = 0;
  r += c == -1; r += c != 256; r += 255 >= c; r += c > 255; r += c >= 255;
  r += s == 200; r += s < A; r += b == 2; r += (c < u) == 2; r += 3 < (c != 1);
  r += u < -1; r += u == -1L; r += sh == 40000; r += s == 4000000000u; r += sh < 0x10000u;
  r += c < 0; r += c >= 0; r += s == B; r += c < 10;
  return r;
}
int m(signed char x) { if (0) return x < 1000; return sizeof(x < 1000); }
int n(signed char x) { switch (x) { case 1000 > 5: return x == 300; } return 0; }
