typedef enum E1 { A1, B1 } TE1;
typedef enum E2 { A2, B2 } TE2;
enum { U1, U2 };
enum C { X, Y = X + A1, Z = Y | A2, W = (TE1)1 + (TE2)1 };
int a1[(A1 < A2) + 1];
int a2[1 + (B1 ? A1 : A2)];
enum F { Q = (int)(A1 + 2.0f) };
int a4[U1 + A1 + 1];
int a5[sizeof(typeof(A1 + A2))];
_Alignas(4 << (A1 & A2)) int a6;
int g(int i) {
    switch (i) { case A1 + A2: return 1; }
    return sizeof(char[(enum E1)1 * (TE2)1]);
}
struct S { unsigned bf : (A1 != B2) + 1; };
