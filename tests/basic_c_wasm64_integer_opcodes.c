typedef unsigned int u32;
typedef long long i64;
typedef unsigned long long u64;

int i32_and(int a, int b) { return a & b; }
int i32_or(int a, int b) { return a | b; }
int i32_xor(int a, int b) { return a ^ b; }
int i32_shl(int a, int b) { return a << b; }
int i32_shr(int a, int b) { return a >> b; }
int i32_eq(int a, int b) { return a == b; }
int i32_ne(int a, int b) { return a != b; }
int i32_lt(int a, int b) { return a < b; }
int i32_gt(int a, int b) { return a > b; }
int i32_le(int a, int b) { return a <= b; }
int i32_ge(int a, int b) { return a >= b; }
int i32_not(int a) { return ~a; }

u32 u32_and(u32 a, u32 b) { return a & b; }
u32 u32_or(u32 a, u32 b) { return a | b; }
u32 u32_xor(u32 a, u32 b) { return a ^ b; }
u32 u32_shl(u32 a, u32 b) { return a << b; }
u32 u32_shr(u32 a, u32 b) { return a >> b; }
int u32_eq(u32 a, u32 b) { return a == b; }
int u32_ne(u32 a, u32 b) { return a != b; }
int u32_lt(u32 a, u32 b) { return a < b; }
int u32_gt(u32 a, u32 b) { return a > b; }
int u32_le(u32 a, u32 b) { return a <= b; }
int u32_ge(u32 a, u32 b) { return a >= b; }
u32 u32_not(u32 a) { return ~a; }

i64 i64_and(i64 a, i64 b) { return a & b; }
i64 i64_or(i64 a, i64 b) { return a | b; }
i64 i64_xor(i64 a, i64 b) { return a ^ b; }
i64 i64_shl(i64 a, i64 b) { return a << b; }
i64 i64_shr(i64 a, i64 b) { return a >> b; }
int i64_eq(i64 a, i64 b) { return a == b; }
int i64_ne(i64 a, i64 b) { return a != b; }
int i64_lt(i64 a, i64 b) { return a < b; }
int i64_gt(i64 a, i64 b) { return a > b; }
int i64_le(i64 a, i64 b) { return a <= b; }
int i64_ge(i64 a, i64 b) { return a >= b; }
i64 i64_not(i64 a) { return ~a; }

u64 u64_and(u64 a, u64 b) { return a & b; }
u64 u64_or(u64 a, u64 b) { return a | b; }
u64 u64_xor(u64 a, u64 b) { return a ^ b; }
u64 u64_shl(u64 a, u64 b) { return a << b; }
u64 u64_shr(u64 a, u64 b) { return a >> b; }
int u64_eq(u64 a, u64 b) { return a == b; }
int u64_ne(u64 a, u64 b) { return a != b; }
int u64_lt(u64 a, u64 b) { return a < b; }
int u64_gt(u64 a, u64 b) { return a > b; }
int u64_le(u64 a, u64 b) { return a <= b; }
int u64_ge(u64 a, u64 b) { return a >= b; }
u64 u64_not(u64 a) { return ~a; }
