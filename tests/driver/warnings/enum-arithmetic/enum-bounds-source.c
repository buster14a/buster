enum E1 { A1, B1 };
enum E2 { A2, B2 };
int arr[A1 + A2];
int s1 = sizeof(char[A1 | A2]);
typedef int T[A1 * A2 + 1];
enum C { X = A1 + A2, Y = B1 == B2, Z = (A1 < A2) };
int f(void) { int local[A1 - A2 + 2]; return sizeof(int[A1 ^ A2 + 1]) + sizeof local; }
_Static_assert(A1 + A2 == 0, "");
struct S { int m[A1 + B2 + 1]; int bf : A1 + B2 + 1; };
int c1 = A1 ? A1 : A2;
enum D { P = (int)(1.0 + A1) };
