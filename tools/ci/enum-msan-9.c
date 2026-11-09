enum E0 { V0 };
enum E1 { V1 };
enum E2 { V2 };
enum E3 { V3 };
enum E4 { V4 };
enum E5 { V5 };
enum E6 { V6 };
enum E7 { V7 };
enum E8 { V8 };
int f(enum E4 mid, int x) { return (V0 == V8) + (V8 == V0) + (mid == V4) + (x == V8); }
