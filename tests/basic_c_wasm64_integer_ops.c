// Operands come from the engine so constant folding cannot hide opcode errors.
#define WASM_INTEGER_OPERATIONS(type, name) \
    type name##_add(type a, type b) { return a + b; } \
    type name##_sub(type a, type b) { return a - b; } \
    type name##_mul(type a, type b) { return a * b; } \
    type name##_div(type a, type b) { return a / b; } \
    type name##_rem(type a, type b) { return a % b; } \
    type name##_and(type a, type b) { return a & b; } \
    type name##_or(type a, type b) { return a | b; } \
    type name##_xor(type a, type b) { return a ^ b; } \
    type name##_shl(type a, type b) { return a << b; } \
    type name##_shr(type a, type b) { return a >> b; } \
    type name##_not(type a) { return ~a; } \
    int name##_eq(type a, type b) { return a == b; } \
    int name##_ne(type a, type b) { return a != b; } \
    int name##_lt(type a, type b) { return a < b; } \
    int name##_le(type a, type b) { return a <= b; } \
    int name##_gt(type a, type b) { return a > b; } \
    int name##_ge(type a, type b) { return a >= b; }

WASM_INTEGER_OPERATIONS(int, s32)
WASM_INTEGER_OPERATIONS(unsigned int, u32)
WASM_INTEGER_OPERATIONS(long long, s64)
WASM_INTEGER_OPERATIONS(unsigned long long, u64)
