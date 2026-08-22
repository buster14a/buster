// The generated middle of the Zig side of the test/c_abi suite: the
// @Vector matrix and the struct-shape families, translated from
// ziglang/zig test/c_abi/main.zig by tools/port_zig_c_abi.py. Edit the
// generator, not this file. The upstream zig_ symbol names are kept so
// the pair links against c_abi_cfuncs.c and stays diffable upstream.
// See tests/c_abi.h for the gate contract.
#include "c_abi.h"

static void assert_or_panic(bool ok) {
    if (!ok) {
        zig_panic();
    }
}

#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_TINY_VECTORS)
typedef uint8_t Vector_1_u8 __attribute__((vector_size(1 * sizeof(uint8_t))));
Vector_1_u8 zig_ret_vector_1_u8(void) {
    return (Vector_1_u8){1};
}
void zig_vector_1_u8(Vector_1_u8 v, size_t i) {
    assert_or_panic(v[0] == 2);
    assert_or_panic(i == 1);
}
Vector_1_u8 c_ret_vector_1_u8(void);
void c_vector_1_u8(Vector_1_u8, size_t);
void c_test_vector_1_u8(void);
static void test_vector_1_u8(void) {
    c_abi_current_test = "@Vector(1, u8)";
#if !(defined(__aarch64__))
    Vector_1_u8 v = c_ret_vector_1_u8();
    assert_or_panic(v[0] == 3);
    c_vector_1_u8((Vector_1_u8){4}, 1);
    c_test_vector_1_u8();
#endif
}
typedef uint8_t Vector_2_u8 __attribute__((vector_size(2 * sizeof(uint8_t))));
Vector_2_u8 zig_ret_vector_2_u8(void) {
    return (Vector_2_u8){ 5, 6 };
}
void zig_vector_2_u8(Vector_2_u8 v, size_t i) {
    assert_or_panic(v[0] == 7);
    assert_or_panic(v[1] == 8);
    assert_or_panic(i == 2);
}
Vector_2_u8 c_ret_vector_2_u8(void);
void c_vector_2_u8(Vector_2_u8, size_t);
void c_test_vector_2_u8(void);
static void test_vector_2_u8(void) {
    c_abi_current_test = "@Vector(2, u8)";
#if !(defined(__aarch64__))
    Vector_2_u8 v = c_ret_vector_2_u8();
    assert_or_panic(v[0] == 9);
    assert_or_panic(v[1] == 10);
    c_vector_2_u8((Vector_2_u8){ 11, 12 }, 2);
    c_test_vector_2_u8();
#endif
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_TINY_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint8_t Vector_3_u8 __attribute__((vector_size(3 * sizeof(uint8_t))));
Vector_3_u8 zig_ret_vector_3_u8(void) {
    return (Vector_3_u8){ 13, 14, 15 };
}
void zig_vector_3_u8(Vector_3_u8 v, size_t i) {
    assert_or_panic(v[0] == 16);
    assert_or_panic(v[1] == 17);
    assert_or_panic(v[2] == 18);
    assert_or_panic(i == 3);
}
Vector_3_u8 c_ret_vector_3_u8(void);
void c_vector_3_u8(Vector_3_u8, size_t);
void c_test_vector_3_u8(void);
static void test_vector_3_u8(void) {
    c_abi_current_test = "@Vector(3, u8)";
#if !(defined(__aarch64__))
    Vector_3_u8 v = c_ret_vector_3_u8();
    assert_or_panic(v[0] == 19);
    assert_or_panic(v[1] == 20);
    assert_or_panic(v[2] == 21);
    c_vector_3_u8((Vector_3_u8){ 22, 23, 24 }, 3);
    c_test_vector_3_u8();
#endif
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint8_t Vector_4_u8 __attribute__((vector_size(4 * sizeof(uint8_t))));
Vector_4_u8 zig_ret_vector_4_u8(void) {
    return (Vector_4_u8){ 25, 26, 27, 28 };
}
void zig_vector_4_u8(Vector_4_u8 v, size_t i) {
    assert_or_panic(v[0] == 29);
    assert_or_panic(v[1] == 30);
    assert_or_panic(v[2] == 31);
    assert_or_panic(v[3] == 32);
    assert_or_panic(i == 4);
}
void zig_vector_4_u8_vector_4_u8(Vector_4_u8 v0, Vector_4_u8 v1, size_t i) {
    assert_or_panic(v0[0] == 33);
    assert_or_panic(v0[1] == 34);
    assert_or_panic(v0[2] == 35);
    assert_or_panic(v0[3] == 36);
    assert_or_panic(v1[0] == 37);
    assert_or_panic(v1[1] == 38);
    assert_or_panic(v1[2] == 39);
    assert_or_panic(v1[3] == 40);
    assert_or_panic(i == 8);
}
Vector_4_u8 c_ret_vector_4_u8(void);
void c_vector_4_u8(Vector_4_u8, size_t);
void c_vector_4_u8_vector_4_u8(Vector_4_u8, Vector_4_u8, size_t);
void c_test_vector_4_u8(void);
static void test_vector_4_u8(void) {
    c_abi_current_test = "@Vector(4, u8)";
#if !(defined(__aarch64__))
    Vector_4_u8 v = c_ret_vector_4_u8();
    assert_or_panic(v[0] == 41);
    assert_or_panic(v[1] == 42);
    assert_or_panic(v[2] == 43);
    assert_or_panic(v[3] == 44);
    c_vector_4_u8((Vector_4_u8){ 45, 46, 47, 48 }, 4);
    c_vector_4_u8_vector_4_u8((Vector_4_u8){ 49, 50, 51, 52 }, (Vector_4_u8){ 53, 54, 55, 56 }, 8);
    c_test_vector_4_u8();
#endif
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint8_t Vector_6_u8 __attribute__((vector_size(6 * sizeof(uint8_t))));
Vector_6_u8 zig_ret_vector_6_u8(void) {
    return (Vector_6_u8){ 41, 42, 43, 44, 45, 46 };
}
void zig_vector_6_u8(Vector_6_u8 v, size_t i) {
    assert_or_panic(v[0] == 47);
    assert_or_panic(v[1] == 48);
    assert_or_panic(v[2] == 49);
    assert_or_panic(v[3] == 50);
    assert_or_panic(v[4] == 51);
    assert_or_panic(v[5] == 52);
    assert_or_panic(i == 6);
}
Vector_6_u8 c_ret_vector_6_u8(void);
void c_vector_6_u8(Vector_6_u8, size_t);
void c_test_vector_6_u8(void);
static void test_vector_6_u8(void) {
    c_abi_current_test = "@Vector(6, u8)";
    Vector_6_u8 v = c_ret_vector_6_u8();
    assert_or_panic(v[0] == 53);
    assert_or_panic(v[1] == 54);
    assert_or_panic(v[2] == 55);
    assert_or_panic(v[3] == 56);
    assert_or_panic(v[4] == 57);
    assert_or_panic(v[5] == 58);
    c_vector_6_u8((Vector_6_u8){ 59, 60, 61, 62, 63, 64 }, 6);
    c_test_vector_6_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint8_t Vector_8_u8 __attribute__((vector_size(8 * sizeof(uint8_t))));
Vector_8_u8 zig_ret_vector_8_u8(void) {
    return (Vector_8_u8){ 65, 66, 67, 68, 69, 70, 71, 72 };
}
void zig_vector_8_u8(Vector_8_u8 v, size_t i) {
    assert_or_panic(v[0] == 73);
    assert_or_panic(v[1] == 74);
    assert_or_panic(v[2] == 75);
    assert_or_panic(v[3] == 76);
    assert_or_panic(v[4] == 77);
    assert_or_panic(v[5] == 78);
    assert_or_panic(v[6] == 79);
    assert_or_panic(v[7] == 80);
    assert_or_panic(i == 8);
}
Vector_8_u8 c_ret_vector_8_u8(void);
void c_vector_8_u8(Vector_8_u8, size_t);
void c_test_vector_8_u8(void);
static void test_vector_8_u8(void) {
    c_abi_current_test = "@Vector(8, u8)";
    Vector_8_u8 v = c_ret_vector_8_u8();
    assert_or_panic(v[0] == 81);
    assert_or_panic(v[1] == 82);
    assert_or_panic(v[2] == 83);
    assert_or_panic(v[3] == 84);
    assert_or_panic(v[4] == 85);
    assert_or_panic(v[5] == 86);
    assert_or_panic(v[6] == 87);
    assert_or_panic(v[7] == 88);
    c_vector_8_u8((Vector_8_u8){ 89, 90, 91, 92, 93, 94, 95, 96 }, 8);
    c_test_vector_8_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint8_t Vector_12_u8 __attribute__((vector_size(12 * sizeof(uint8_t))));
Vector_12_u8 zig_ret_vector_12_u8(void) {
    return (Vector_12_u8){ 97, 98, 99, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
}
void zig_vector_12_u8(Vector_12_u8 v, size_t i) {
    assert_or_panic(v[0] == 9);
    assert_or_panic(v[1] == 10);
    assert_or_panic(v[2] == 11);
    assert_or_panic(v[3] == 12);
    assert_or_panic(v[4] == 13);
    assert_or_panic(v[5] == 14);
    assert_or_panic(v[6] == 15);
    assert_or_panic(v[7] == 16);
    assert_or_panic(v[8] == 17);
    assert_or_panic(v[9] == 18);
    assert_or_panic(v[10] == 19);
    assert_or_panic(v[11] == 20);
    assert_or_panic(i == 12);
}
Vector_12_u8 c_ret_vector_12_u8(void);
void c_vector_12_u8(Vector_12_u8, size_t);
void c_test_vector_12_u8(void);
static void test_vector_12_u8(void) {
    c_abi_current_test = "@Vector(12, u8)";
    Vector_12_u8 v = c_ret_vector_12_u8();
    assert_or_panic(v[0] == 21);
    assert_or_panic(v[1] == 22);
    assert_or_panic(v[2] == 23);
    assert_or_panic(v[3] == 24);
    assert_or_panic(v[4] == 25);
    assert_or_panic(v[5] == 26);
    assert_or_panic(v[6] == 27);
    assert_or_panic(v[7] == 28);
    assert_or_panic(v[8] == 29);
    assert_or_panic(v[9] == 30);
    assert_or_panic(v[10] == 31);
    assert_or_panic(v[11] == 32);
    c_vector_12_u8((Vector_12_u8){ 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44 }, 12);
    c_test_vector_12_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint8_t Vector_16_u8 __attribute__((vector_size(16 * sizeof(uint8_t))));
Vector_16_u8 zig_ret_vector_16_u8(void) {
    return (Vector_16_u8){ 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60 };
}
void zig_vector_16_u8(Vector_16_u8 v, size_t i) {
    assert_or_panic(v[0] == 61);
    assert_or_panic(v[1] == 62);
    assert_or_panic(v[2] == 63);
    assert_or_panic(v[3] == 64);
    assert_or_panic(v[4] == 65);
    assert_or_panic(v[5] == 66);
    assert_or_panic(v[6] == 67);
    assert_or_panic(v[7] == 68);
    assert_or_panic(v[8] == 69);
    assert_or_panic(v[9] == 70);
    assert_or_panic(v[10] == 71);
    assert_or_panic(v[11] == 72);
    assert_or_panic(v[12] == 73);
    assert_or_panic(v[13] == 74);
    assert_or_panic(v[14] == 75);
    assert_or_panic(v[15] == 76);
    assert_or_panic(i == 16);
}
Vector_16_u8 c_ret_vector_16_u8(void);
void c_vector_16_u8(Vector_16_u8, size_t);
void c_test_vector_16_u8(void);
static void test_vector_16_u8(void) {
    c_abi_current_test = "@Vector(16, u8)";
    Vector_16_u8 v = c_ret_vector_16_u8();
    assert_or_panic(v[0] == 77);
    assert_or_panic(v[1] == 78);
    assert_or_panic(v[2] == 79);
    assert_or_panic(v[3] == 80);
    assert_or_panic(v[4] == 81);
    assert_or_panic(v[5] == 82);
    assert_or_panic(v[6] == 83);
    assert_or_panic(v[7] == 84);
    assert_or_panic(v[8] == 85);
    assert_or_panic(v[9] == 86);
    assert_or_panic(v[10] == 87);
    assert_or_panic(v[11] == 88);
    assert_or_panic(v[12] == 89);
    assert_or_panic(v[13] == 90);
    assert_or_panic(v[14] == 91);
    assert_or_panic(v[15] == 92);
    c_vector_16_u8((Vector_16_u8){ 93, 94, 95, 96, 97, 98, 99, 0, 1, 2, 3, 4, 5, 6, 7, 8 }, 16);
    c_test_vector_16_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint8_t Vector_24_u8 __attribute__((vector_size(24 * sizeof(uint8_t))));
Vector_24_u8 zig_ret_vector_24_u8(void) {
    return (Vector_24_u8){
    9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32,
    };
}
void zig_vector_24_u8(Vector_24_u8 v, size_t i) {
    assert_or_panic(v[0] == 33);
    assert_or_panic(v[1] == 34);
    assert_or_panic(v[2] == 35);
    assert_or_panic(v[3] == 36);
    assert_or_panic(v[4] == 37);
    assert_or_panic(v[5] == 38);
    assert_or_panic(v[6] == 39);
    assert_or_panic(v[7] == 40);
    assert_or_panic(v[8] == 41);
    assert_or_panic(v[9] == 42);
    assert_or_panic(v[10] == 43);
    assert_or_panic(v[11] == 44);
    assert_or_panic(v[12] == 45);
    assert_or_panic(v[13] == 46);
    assert_or_panic(v[14] == 47);
    assert_or_panic(v[15] == 48);
    assert_or_panic(v[16] == 49);
    assert_or_panic(v[17] == 50);
    assert_or_panic(v[18] == 51);
    assert_or_panic(v[19] == 52);
    assert_or_panic(v[20] == 53);
    assert_or_panic(v[21] == 54);
    assert_or_panic(v[22] == 55);
    assert_or_panic(v[23] == 56);
    assert_or_panic(i == 24);
}
Vector_24_u8 c_ret_vector_24_u8(void);
void c_vector_24_u8(Vector_24_u8, size_t);
void c_test_vector_24_u8(void);
static void test_vector_24_u8(void) {
    c_abi_current_test = "@Vector(24, u8)";
    Vector_24_u8 v = c_ret_vector_24_u8();
    assert_or_panic(v[0] == 57);
    assert_or_panic(v[1] == 58);
    assert_or_panic(v[2] == 59);
    assert_or_panic(v[3] == 60);
    assert_or_panic(v[4] == 61);
    assert_or_panic(v[5] == 62);
    assert_or_panic(v[6] == 63);
    assert_or_panic(v[7] == 64);
    assert_or_panic(v[8] == 65);
    assert_or_panic(v[9] == 66);
    assert_or_panic(v[10] == 67);
    assert_or_panic(v[11] == 68);
    assert_or_panic(v[12] == 69);
    assert_or_panic(v[13] == 70);
    assert_or_panic(v[14] == 71);
    assert_or_panic(v[15] == 72);
    assert_or_panic(v[16] == 73);
    assert_or_panic(v[17] == 74);
    assert_or_panic(v[18] == 75);
    assert_or_panic(v[19] == 76);
    assert_or_panic(v[20] == 77);
    assert_or_panic(v[21] == 78);
    assert_or_panic(v[22] == 79);
    assert_or_panic(v[23] == 80);
    c_vector_24_u8((Vector_24_u8){
        81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96,
        97, 98, 99, 0,  1,  2,  3,  4,
    }, 24);
    c_test_vector_24_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint8_t Vector_32_u8 __attribute__((vector_size(32 * sizeof(uint8_t))));
Vector_32_u8 zig_ret_vector_32_u8(void) {
    return (Vector_32_u8){
    5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    };
}
void zig_vector_32_u8(Vector_32_u8 v, size_t i) {
    assert_or_panic(v[0] == 37);
    assert_or_panic(v[1] == 38);
    assert_or_panic(v[2] == 39);
    assert_or_panic(v[3] == 40);
    assert_or_panic(v[4] == 41);
    assert_or_panic(v[5] == 42);
    assert_or_panic(v[6] == 43);
    assert_or_panic(v[7] == 44);
    assert_or_panic(v[8] == 45);
    assert_or_panic(v[9] == 46);
    assert_or_panic(v[10] == 47);
    assert_or_panic(v[11] == 48);
    assert_or_panic(v[12] == 49);
    assert_or_panic(v[13] == 50);
    assert_or_panic(v[14] == 51);
    assert_or_panic(v[15] == 52);
    assert_or_panic(v[16] == 53);
    assert_or_panic(v[17] == 54);
    assert_or_panic(v[18] == 55);
    assert_or_panic(v[19] == 56);
    assert_or_panic(v[20] == 57);
    assert_or_panic(v[21] == 58);
    assert_or_panic(v[22] == 59);
    assert_or_panic(v[23] == 60);
    assert_or_panic(v[24] == 61);
    assert_or_panic(v[25] == 62);
    assert_or_panic(v[26] == 63);
    assert_or_panic(v[27] == 64);
    assert_or_panic(v[28] == 65);
    assert_or_panic(v[29] == 66);
    assert_or_panic(v[30] == 67);
    assert_or_panic(v[31] == 68);
    assert_or_panic(i == 32);
}
Vector_32_u8 c_ret_vector_32_u8(void);
void c_vector_32_u8(Vector_32_u8, size_t);
void c_test_vector_32_u8(void);
static void test_vector_32_u8(void) {
    c_abi_current_test = "@Vector(32, u8)";
    Vector_32_u8 v = c_ret_vector_32_u8();
    assert_or_panic(v[0] == 69);
    assert_or_panic(v[1] == 70);
    assert_or_panic(v[2] == 71);
    assert_or_panic(v[3] == 72);
    assert_or_panic(v[4] == 73);
    assert_or_panic(v[5] == 74);
    assert_or_panic(v[6] == 75);
    assert_or_panic(v[7] == 76);
    assert_or_panic(v[8] == 77);
    assert_or_panic(v[9] == 78);
    assert_or_panic(v[10] == 79);
    assert_or_panic(v[11] == 80);
    assert_or_panic(v[12] == 81);
    assert_or_panic(v[13] == 82);
    assert_or_panic(v[14] == 83);
    assert_or_panic(v[15] == 84);
    assert_or_panic(v[16] == 85);
    assert_or_panic(v[17] == 86);
    assert_or_panic(v[18] == 87);
    assert_or_panic(v[19] == 88);
    assert_or_panic(v[20] == 89);
    assert_or_panic(v[21] == 90);
    assert_or_panic(v[22] == 91);
    assert_or_panic(v[23] == 92);
    assert_or_panic(v[24] == 93);
    assert_or_panic(v[25] == 94);
    assert_or_panic(v[26] == 95);
    assert_or_panic(v[27] == 96);
    assert_or_panic(v[28] == 97);
    assert_or_panic(v[29] == 98);
    assert_or_panic(v[30] == 99);
    assert_or_panic(v[31] == 0);
    c_vector_32_u8((Vector_32_u8){
        1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    }, 32);
    c_test_vector_32_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint8_t Vector_48_u8 __attribute__((vector_size(48 * sizeof(uint8_t))));
Vector_48_u8 zig_ret_vector_48_u8(void) {
    return (Vector_48_u8){
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
    };
}
void zig_vector_48_u8(Vector_48_u8 v, size_t i) {
    assert_or_panic(v[0] == 81);
    assert_or_panic(v[1] == 82);
    assert_or_panic(v[2] == 83);
    assert_or_panic(v[3] == 84);
    assert_or_panic(v[4] == 85);
    assert_or_panic(v[5] == 86);
    assert_or_panic(v[6] == 87);
    assert_or_panic(v[7] == 88);
    assert_or_panic(v[8] == 89);
    assert_or_panic(v[9] == 90);
    assert_or_panic(v[10] == 91);
    assert_or_panic(v[11] == 92);
    assert_or_panic(v[12] == 93);
    assert_or_panic(v[13] == 94);
    assert_or_panic(v[14] == 95);
    assert_or_panic(v[15] == 96);
    assert_or_panic(v[16] == 97);
    assert_or_panic(v[17] == 98);
    assert_or_panic(v[18] == 99);
    assert_or_panic(v[19] == 0);
    assert_or_panic(v[20] == 1);
    assert_or_panic(v[21] == 2);
    assert_or_panic(v[22] == 3);
    assert_or_panic(v[23] == 4);
    assert_or_panic(v[24] == 5);
    assert_or_panic(v[25] == 6);
    assert_or_panic(v[26] == 7);
    assert_or_panic(v[27] == 8);
    assert_or_panic(v[28] == 9);
    assert_or_panic(v[29] == 10);
    assert_or_panic(v[30] == 11);
    assert_or_panic(v[31] == 12);
    assert_or_panic(v[32] == 13);
    assert_or_panic(v[33] == 14);
    assert_or_panic(v[34] == 15);
    assert_or_panic(v[35] == 16);
    assert_or_panic(v[36] == 17);
    assert_or_panic(v[37] == 18);
    assert_or_panic(v[38] == 19);
    assert_or_panic(v[39] == 20);
    assert_or_panic(v[40] == 21);
    assert_or_panic(v[41] == 22);
    assert_or_panic(v[42] == 23);
    assert_or_panic(v[43] == 24);
    assert_or_panic(v[44] == 25);
    assert_or_panic(v[45] == 26);
    assert_or_panic(v[46] == 27);
    assert_or_panic(v[47] == 28);
    assert_or_panic(i == 48);
}
Vector_48_u8 c_ret_vector_48_u8(void);
void c_vector_48_u8(Vector_48_u8, size_t);
void c_test_vector_48_u8(void);
static void test_vector_48_u8(void) {
    c_abi_current_test = "@Vector(48, u8)";
    Vector_48_u8 v = c_ret_vector_48_u8();
    assert_or_panic(v[0] == 29);
    assert_or_panic(v[1] == 30);
    assert_or_panic(v[2] == 31);
    assert_or_panic(v[3] == 32);
    assert_or_panic(v[4] == 33);
    assert_or_panic(v[5] == 34);
    assert_or_panic(v[6] == 35);
    assert_or_panic(v[7] == 36);
    assert_or_panic(v[8] == 37);
    assert_or_panic(v[9] == 38);
    assert_or_panic(v[10] == 39);
    assert_or_panic(v[11] == 40);
    assert_or_panic(v[12] == 41);
    assert_or_panic(v[13] == 42);
    assert_or_panic(v[14] == 43);
    assert_or_panic(v[15] == 44);
    assert_or_panic(v[16] == 45);
    assert_or_panic(v[17] == 46);
    assert_or_panic(v[18] == 47);
    assert_or_panic(v[19] == 48);
    assert_or_panic(v[20] == 49);
    assert_or_panic(v[21] == 50);
    assert_or_panic(v[22] == 51);
    assert_or_panic(v[23] == 52);
    assert_or_panic(v[24] == 53);
    assert_or_panic(v[25] == 54);
    assert_or_panic(v[26] == 55);
    assert_or_panic(v[27] == 56);
    assert_or_panic(v[28] == 57);
    assert_or_panic(v[29] == 58);
    assert_or_panic(v[30] == 59);
    assert_or_panic(v[31] == 60);
    assert_or_panic(v[32] == 61);
    assert_or_panic(v[33] == 62);
    assert_or_panic(v[34] == 63);
    assert_or_panic(v[35] == 64);
    assert_or_panic(v[36] == 65);
    assert_or_panic(v[37] == 66);
    assert_or_panic(v[38] == 67);
    assert_or_panic(v[39] == 68);
    assert_or_panic(v[40] == 69);
    assert_or_panic(v[41] == 70);
    assert_or_panic(v[42] == 71);
    assert_or_panic(v[43] == 72);
    assert_or_panic(v[44] == 73);
    assert_or_panic(v[45] == 74);
    assert_or_panic(v[46] == 75);
    assert_or_panic(v[47] == 76);
    c_vector_48_u8((Vector_48_u8){
        77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92,
        93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,
        9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    }, 48);
    c_test_vector_48_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint8_t Vector_64_u8 __attribute__((vector_size(64 * sizeof(uint8_t))));
Vector_64_u8 zig_ret_vector_64_u8(void) {
    return (Vector_64_u8){
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
    57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72,
    73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88,
    };
}
void zig_vector_64_u8(Vector_64_u8 v, size_t i) {
    assert_or_panic(v[0] == 89);
    assert_or_panic(v[1] == 90);
    assert_or_panic(v[2] == 91);
    assert_or_panic(v[3] == 92);
    assert_or_panic(v[4] == 93);
    assert_or_panic(v[5] == 94);
    assert_or_panic(v[6] == 95);
    assert_or_panic(v[7] == 96);
    assert_or_panic(v[8] == 97);
    assert_or_panic(v[9] == 98);
    assert_or_panic(v[10] == 99);
    assert_or_panic(v[11] == 0);
    assert_or_panic(v[12] == 1);
    assert_or_panic(v[13] == 2);
    assert_or_panic(v[14] == 3);
    assert_or_panic(v[15] == 4);
    assert_or_panic(v[16] == 5);
    assert_or_panic(v[17] == 6);
    assert_or_panic(v[18] == 7);
    assert_or_panic(v[19] == 8);
    assert_or_panic(v[20] == 9);
    assert_or_panic(v[21] == 10);
    assert_or_panic(v[22] == 11);
    assert_or_panic(v[23] == 12);
    assert_or_panic(v[24] == 13);
    assert_or_panic(v[25] == 14);
    assert_or_panic(v[26] == 15);
    assert_or_panic(v[27] == 16);
    assert_or_panic(v[28] == 17);
    assert_or_panic(v[29] == 18);
    assert_or_panic(v[30] == 19);
    assert_or_panic(v[31] == 20);
    assert_or_panic(v[32] == 21);
    assert_or_panic(v[33] == 22);
    assert_or_panic(v[34] == 23);
    assert_or_panic(v[35] == 24);
    assert_or_panic(v[36] == 25);
    assert_or_panic(v[37] == 26);
    assert_or_panic(v[38] == 27);
    assert_or_panic(v[39] == 28);
    assert_or_panic(v[40] == 29);
    assert_or_panic(v[41] == 30);
    assert_or_panic(v[42] == 31);
    assert_or_panic(v[43] == 32);
    assert_or_panic(v[44] == 33);
    assert_or_panic(v[45] == 34);
    assert_or_panic(v[46] == 35);
    assert_or_panic(v[47] == 36);
    assert_or_panic(v[48] == 37);
    assert_or_panic(v[49] == 38);
    assert_or_panic(v[50] == 39);
    assert_or_panic(v[51] == 40);
    assert_or_panic(v[52] == 41);
    assert_or_panic(v[53] == 42);
    assert_or_panic(v[54] == 43);
    assert_or_panic(v[55] == 44);
    assert_or_panic(v[56] == 45);
    assert_or_panic(v[57] == 46);
    assert_or_panic(v[58] == 47);
    assert_or_panic(v[59] == 48);
    assert_or_panic(v[60] == 49);
    assert_or_panic(v[61] == 50);
    assert_or_panic(v[62] == 51);
    assert_or_panic(v[63] == 52);
    assert_or_panic(i == 64);
}
Vector_64_u8 c_ret_vector_64_u8(void);
void c_vector_64_u8(Vector_64_u8, size_t);
void c_test_vector_64_u8(void);
static void test_vector_64_u8(void) {
    c_abi_current_test = "@Vector(64, u8)";
    Vector_64_u8 v = c_ret_vector_64_u8();
    assert_or_panic(v[0] == 53);
    assert_or_panic(v[1] == 54);
    assert_or_panic(v[2] == 55);
    assert_or_panic(v[3] == 56);
    assert_or_panic(v[4] == 57);
    assert_or_panic(v[5] == 58);
    assert_or_panic(v[6] == 59);
    assert_or_panic(v[7] == 60);
    assert_or_panic(v[8] == 61);
    assert_or_panic(v[9] == 62);
    assert_or_panic(v[10] == 63);
    assert_or_panic(v[11] == 64);
    assert_or_panic(v[12] == 65);
    assert_or_panic(v[13] == 66);
    assert_or_panic(v[14] == 67);
    assert_or_panic(v[15] == 68);
    assert_or_panic(v[16] == 69);
    assert_or_panic(v[17] == 70);
    assert_or_panic(v[18] == 71);
    assert_or_panic(v[19] == 72);
    assert_or_panic(v[20] == 73);
    assert_or_panic(v[21] == 74);
    assert_or_panic(v[22] == 75);
    assert_or_panic(v[23] == 76);
    assert_or_panic(v[24] == 77);
    assert_or_panic(v[25] == 78);
    assert_or_panic(v[26] == 79);
    assert_or_panic(v[27] == 80);
    assert_or_panic(v[28] == 81);
    assert_or_panic(v[29] == 82);
    assert_or_panic(v[30] == 83);
    assert_or_panic(v[31] == 84);
    assert_or_panic(v[32] == 85);
    assert_or_panic(v[33] == 86);
    assert_or_panic(v[34] == 87);
    assert_or_panic(v[35] == 88);
    assert_or_panic(v[36] == 89);
    assert_or_panic(v[37] == 90);
    assert_or_panic(v[38] == 91);
    assert_or_panic(v[39] == 92);
    assert_or_panic(v[40] == 93);
    assert_or_panic(v[41] == 94);
    assert_or_panic(v[42] == 95);
    assert_or_panic(v[43] == 96);
    assert_or_panic(v[44] == 97);
    assert_or_panic(v[45] == 98);
    assert_or_panic(v[46] == 99);
    assert_or_panic(v[47] == 0);
    assert_or_panic(v[48] == 1);
    assert_or_panic(v[49] == 2);
    assert_or_panic(v[50] == 3);
    assert_or_panic(v[51] == 4);
    assert_or_panic(v[52] == 5);
    assert_or_panic(v[53] == 6);
    assert_or_panic(v[54] == 7);
    assert_or_panic(v[55] == 8);
    assert_or_panic(v[56] == 9);
    assert_or_panic(v[57] == 10);
    assert_or_panic(v[58] == 11);
    assert_or_panic(v[59] == 12);
    assert_or_panic(v[60] == 13);
    assert_or_panic(v[61] == 14);
    assert_or_panic(v[62] == 15);
    assert_or_panic(v[63] == 16);
    c_vector_64_u8((Vector_64_u8){
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
        33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
        49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
        65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
    }, 64);
    c_test_vector_64_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint8_t Vector_96_u8 __attribute__((vector_size(96 * sizeof(uint8_t))));
Vector_96_u8 zig_ret_vector_96_u8(void) {
    return (Vector_96_u8){
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
    6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
    };
}
void zig_vector_96_u8(Vector_96_u8 v, size_t i) {
    assert_or_panic(v[0] == 86);
    assert_or_panic(v[1] == 87);
    assert_or_panic(v[2] == 88);
    assert_or_panic(v[3] == 89);
    assert_or_panic(v[4] == 90);
    assert_or_panic(v[5] == 91);
    assert_or_panic(v[6] == 92);
    assert_or_panic(v[7] == 93);
    assert_or_panic(v[8] == 94);
    assert_or_panic(v[9] == 95);
    assert_or_panic(v[10] == 96);
    assert_or_panic(v[11] == 97);
    assert_or_panic(v[12] == 98);
    assert_or_panic(v[13] == 99);
    assert_or_panic(v[14] == 0);
    assert_or_panic(v[15] == 1);
    assert_or_panic(v[16] == 2);
    assert_or_panic(v[17] == 3);
    assert_or_panic(v[18] == 4);
    assert_or_panic(v[19] == 5);
    assert_or_panic(v[20] == 6);
    assert_or_panic(v[21] == 7);
    assert_or_panic(v[22] == 8);
    assert_or_panic(v[23] == 9);
    assert_or_panic(v[24] == 10);
    assert_or_panic(v[25] == 11);
    assert_or_panic(v[26] == 12);
    assert_or_panic(v[27] == 13);
    assert_or_panic(v[28] == 14);
    assert_or_panic(v[29] == 15);
    assert_or_panic(v[30] == 16);
    assert_or_panic(v[31] == 17);
    assert_or_panic(v[32] == 18);
    assert_or_panic(v[33] == 19);
    assert_or_panic(v[34] == 20);
    assert_or_panic(v[35] == 21);
    assert_or_panic(v[36] == 22);
    assert_or_panic(v[37] == 23);
    assert_or_panic(v[38] == 24);
    assert_or_panic(v[39] == 25);
    assert_or_panic(v[40] == 26);
    assert_or_panic(v[41] == 27);
    assert_or_panic(v[42] == 28);
    assert_or_panic(v[43] == 29);
    assert_or_panic(v[44] == 30);
    assert_or_panic(v[45] == 31);
    assert_or_panic(v[46] == 32);
    assert_or_panic(v[47] == 33);
    assert_or_panic(v[48] == 34);
    assert_or_panic(v[49] == 35);
    assert_or_panic(v[50] == 36);
    assert_or_panic(v[51] == 37);
    assert_or_panic(v[52] == 38);
    assert_or_panic(v[53] == 39);
    assert_or_panic(v[54] == 40);
    assert_or_panic(v[55] == 41);
    assert_or_panic(v[56] == 42);
    assert_or_panic(v[57] == 43);
    assert_or_panic(v[58] == 44);
    assert_or_panic(v[59] == 45);
    assert_or_panic(v[60] == 46);
    assert_or_panic(v[61] == 47);
    assert_or_panic(v[62] == 48);
    assert_or_panic(v[63] == 49);
    assert_or_panic(v[64] == 50);
    assert_or_panic(v[65] == 51);
    assert_or_panic(v[66] == 52);
    assert_or_panic(v[67] == 53);
    assert_or_panic(v[68] == 54);
    assert_or_panic(v[69] == 55);
    assert_or_panic(v[70] == 56);
    assert_or_panic(v[71] == 57);
    assert_or_panic(v[72] == 58);
    assert_or_panic(v[73] == 59);
    assert_or_panic(v[74] == 60);
    assert_or_panic(v[75] == 61);
    assert_or_panic(v[76] == 62);
    assert_or_panic(v[77] == 63);
    assert_or_panic(v[78] == 64);
    assert_or_panic(v[79] == 65);
    assert_or_panic(v[80] == 66);
    assert_or_panic(v[81] == 67);
    assert_or_panic(v[82] == 68);
    assert_or_panic(v[83] == 69);
    assert_or_panic(v[84] == 70);
    assert_or_panic(v[85] == 71);
    assert_or_panic(v[86] == 72);
    assert_or_panic(v[87] == 73);
    assert_or_panic(v[88] == 74);
    assert_or_panic(v[89] == 75);
    assert_or_panic(v[90] == 76);
    assert_or_panic(v[91] == 77);
    assert_or_panic(v[92] == 78);
    assert_or_panic(v[93] == 79);
    assert_or_panic(v[94] == 80);
    assert_or_panic(v[95] == 81);
    assert_or_panic(i == 96);
}
Vector_96_u8 c_ret_vector_96_u8(void);
void c_vector_96_u8(Vector_96_u8, size_t);
void c_test_vector_96_u8(void);
static void test_vector_96_u8(void) {
    c_abi_current_test = "@Vector(96, u8)";
    Vector_96_u8 v = c_ret_vector_96_u8();
    assert_or_panic(v[0] == 82);
    assert_or_panic(v[1] == 83);
    assert_or_panic(v[2] == 84);
    assert_or_panic(v[3] == 85);
    assert_or_panic(v[4] == 86);
    assert_or_panic(v[5] == 87);
    assert_or_panic(v[6] == 88);
    assert_or_panic(v[7] == 89);
    assert_or_panic(v[8] == 90);
    assert_or_panic(v[9] == 91);
    assert_or_panic(v[10] == 92);
    assert_or_panic(v[11] == 93);
    assert_or_panic(v[12] == 94);
    assert_or_panic(v[13] == 95);
    assert_or_panic(v[14] == 96);
    assert_or_panic(v[15] == 97);
    assert_or_panic(v[16] == 98);
    assert_or_panic(v[17] == 99);
    assert_or_panic(v[18] == 0);
    assert_or_panic(v[19] == 1);
    assert_or_panic(v[20] == 2);
    assert_or_panic(v[21] == 3);
    assert_or_panic(v[22] == 4);
    assert_or_panic(v[23] == 5);
    assert_or_panic(v[24] == 6);
    assert_or_panic(v[25] == 7);
    assert_or_panic(v[26] == 8);
    assert_or_panic(v[27] == 9);
    assert_or_panic(v[28] == 10);
    assert_or_panic(v[29] == 11);
    assert_or_panic(v[30] == 12);
    assert_or_panic(v[31] == 13);
    assert_or_panic(v[32] == 14);
    assert_or_panic(v[33] == 15);
    assert_or_panic(v[34] == 16);
    assert_or_panic(v[35] == 17);
    assert_or_panic(v[36] == 18);
    assert_or_panic(v[37] == 19);
    assert_or_panic(v[38] == 20);
    assert_or_panic(v[39] == 21);
    assert_or_panic(v[40] == 22);
    assert_or_panic(v[41] == 23);
    assert_or_panic(v[42] == 24);
    assert_or_panic(v[43] == 25);
    assert_or_panic(v[44] == 26);
    assert_or_panic(v[45] == 27);
    assert_or_panic(v[46] == 28);
    assert_or_panic(v[47] == 29);
    assert_or_panic(v[48] == 30);
    assert_or_panic(v[49] == 31);
    assert_or_panic(v[50] == 32);
    assert_or_panic(v[51] == 33);
    assert_or_panic(v[52] == 34);
    assert_or_panic(v[53] == 35);
    assert_or_panic(v[54] == 36);
    assert_or_panic(v[55] == 37);
    assert_or_panic(v[56] == 38);
    assert_or_panic(v[57] == 39);
    assert_or_panic(v[58] == 40);
    assert_or_panic(v[59] == 41);
    assert_or_panic(v[60] == 42);
    assert_or_panic(v[61] == 43);
    assert_or_panic(v[62] == 44);
    assert_or_panic(v[63] == 45);
    assert_or_panic(v[64] == 46);
    assert_or_panic(v[65] == 47);
    assert_or_panic(v[66] == 48);
    assert_or_panic(v[67] == 49);
    assert_or_panic(v[68] == 50);
    assert_or_panic(v[69] == 51);
    assert_or_panic(v[70] == 52);
    assert_or_panic(v[71] == 53);
    assert_or_panic(v[72] == 54);
    assert_or_panic(v[73] == 55);
    assert_or_panic(v[74] == 56);
    assert_or_panic(v[75] == 57);
    assert_or_panic(v[76] == 58);
    assert_or_panic(v[77] == 59);
    assert_or_panic(v[78] == 60);
    assert_or_panic(v[79] == 61);
    assert_or_panic(v[80] == 62);
    assert_or_panic(v[81] == 63);
    assert_or_panic(v[82] == 64);
    assert_or_panic(v[83] == 65);
    assert_or_panic(v[84] == 66);
    assert_or_panic(v[85] == 67);
    assert_or_panic(v[86] == 68);
    assert_or_panic(v[87] == 69);
    assert_or_panic(v[88] == 70);
    assert_or_panic(v[89] == 71);
    assert_or_panic(v[90] == 72);
    assert_or_panic(v[91] == 73);
    assert_or_panic(v[92] == 74);
    assert_or_panic(v[93] == 75);
    assert_or_panic(v[94] == 76);
    assert_or_panic(v[95] == 77);
    c_vector_96_u8((Vector_96_u8){
        78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
        94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
        42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
        58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
    }, 96);
    c_test_vector_96_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint8_t Vector_128_u8 __attribute__((vector_size(128 * sizeof(uint8_t))));
Vector_128_u8 zig_ret_vector_128_u8(void) {
    return (Vector_128_u8){
    74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
    6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
    86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
    };
}
void zig_vector_128_u8(Vector_128_u8 v, size_t i) {
    assert_or_panic(v[0] == 2);
    assert_or_panic(v[1] == 3);
    assert_or_panic(v[2] == 4);
    assert_or_panic(v[3] == 5);
    assert_or_panic(v[4] == 6);
    assert_or_panic(v[5] == 7);
    assert_or_panic(v[6] == 8);
    assert_or_panic(v[7] == 9);
    assert_or_panic(v[8] == 10);
    assert_or_panic(v[9] == 11);
    assert_or_panic(v[10] == 12);
    assert_or_panic(v[11] == 13);
    assert_or_panic(v[12] == 14);
    assert_or_panic(v[13] == 15);
    assert_or_panic(v[14] == 16);
    assert_or_panic(v[15] == 17);
    assert_or_panic(v[16] == 18);
    assert_or_panic(v[17] == 19);
    assert_or_panic(v[18] == 20);
    assert_or_panic(v[19] == 21);
    assert_or_panic(v[20] == 22);
    assert_or_panic(v[21] == 23);
    assert_or_panic(v[22] == 24);
    assert_or_panic(v[23] == 25);
    assert_or_panic(v[24] == 26);
    assert_or_panic(v[25] == 27);
    assert_or_panic(v[26] == 28);
    assert_or_panic(v[27] == 29);
    assert_or_panic(v[28] == 30);
    assert_or_panic(v[29] == 31);
    assert_or_panic(v[30] == 32);
    assert_or_panic(v[31] == 33);
    assert_or_panic(v[32] == 34);
    assert_or_panic(v[33] == 35);
    assert_or_panic(v[34] == 36);
    assert_or_panic(v[35] == 37);
    assert_or_panic(v[36] == 38);
    assert_or_panic(v[37] == 39);
    assert_or_panic(v[38] == 40);
    assert_or_panic(v[39] == 41);
    assert_or_panic(v[40] == 42);
    assert_or_panic(v[41] == 43);
    assert_or_panic(v[42] == 44);
    assert_or_panic(v[43] == 45);
    assert_or_panic(v[44] == 46);
    assert_or_panic(v[45] == 47);
    assert_or_panic(v[46] == 48);
    assert_or_panic(v[47] == 49);
    assert_or_panic(v[48] == 50);
    assert_or_panic(v[49] == 51);
    assert_or_panic(v[50] == 52);
    assert_or_panic(v[51] == 53);
    assert_or_panic(v[52] == 54);
    assert_or_panic(v[53] == 55);
    assert_or_panic(v[54] == 56);
    assert_or_panic(v[55] == 57);
    assert_or_panic(v[56] == 58);
    assert_or_panic(v[57] == 59);
    assert_or_panic(v[58] == 60);
    assert_or_panic(v[59] == 61);
    assert_or_panic(v[60] == 62);
    assert_or_panic(v[61] == 63);
    assert_or_panic(v[62] == 64);
    assert_or_panic(v[63] == 65);
    assert_or_panic(v[64] == 66);
    assert_or_panic(v[65] == 67);
    assert_or_panic(v[66] == 68);
    assert_or_panic(v[67] == 69);
    assert_or_panic(v[68] == 70);
    assert_or_panic(v[69] == 71);
    assert_or_panic(v[70] == 72);
    assert_or_panic(v[71] == 73);
    assert_or_panic(v[72] == 74);
    assert_or_panic(v[73] == 75);
    assert_or_panic(v[74] == 76);
    assert_or_panic(v[75] == 77);
    assert_or_panic(v[76] == 78);
    assert_or_panic(v[77] == 79);
    assert_or_panic(v[78] == 80);
    assert_or_panic(v[79] == 81);
    assert_or_panic(v[80] == 82);
    assert_or_panic(v[81] == 83);
    assert_or_panic(v[82] == 84);
    assert_or_panic(v[83] == 85);
    assert_or_panic(v[84] == 86);
    assert_or_panic(v[85] == 87);
    assert_or_panic(v[86] == 88);
    assert_or_panic(v[87] == 89);
    assert_or_panic(v[88] == 90);
    assert_or_panic(v[89] == 91);
    assert_or_panic(v[90] == 92);
    assert_or_panic(v[91] == 93);
    assert_or_panic(v[92] == 94);
    assert_or_panic(v[93] == 95);
    assert_or_panic(v[94] == 96);
    assert_or_panic(v[95] == 97);
    assert_or_panic(v[96] == 98);
    assert_or_panic(v[97] == 99);
    assert_or_panic(v[98] == 0);
    assert_or_panic(v[99] == 1);
    assert_or_panic(v[100] == 2);
    assert_or_panic(v[101] == 3);
    assert_or_panic(v[102] == 4);
    assert_or_panic(v[103] == 5);
    assert_or_panic(v[104] == 6);
    assert_or_panic(v[105] == 7);
    assert_or_panic(v[106] == 8);
    assert_or_panic(v[107] == 9);
    assert_or_panic(v[108] == 10);
    assert_or_panic(v[109] == 11);
    assert_or_panic(v[110] == 12);
    assert_or_panic(v[111] == 13);
    assert_or_panic(v[112] == 14);
    assert_or_panic(v[113] == 15);
    assert_or_panic(v[114] == 16);
    assert_or_panic(v[115] == 17);
    assert_or_panic(v[116] == 18);
    assert_or_panic(v[117] == 19);
    assert_or_panic(v[118] == 20);
    assert_or_panic(v[119] == 21);
    assert_or_panic(v[120] == 22);
    assert_or_panic(v[121] == 23);
    assert_or_panic(v[122] == 24);
    assert_or_panic(v[123] == 25);
    assert_or_panic(v[124] == 26);
    assert_or_panic(v[125] == 27);
    assert_or_panic(v[126] == 28);
    assert_or_panic(v[127] == 29);
    assert_or_panic(i == 128);
}
Vector_128_u8 c_ret_vector_128_u8(void);
void c_vector_128_u8(Vector_128_u8, size_t);
void c_test_vector_128_u8(void);
static void test_vector_128_u8(void) {
    c_abi_current_test = "@Vector(128, u8)";
    Vector_128_u8 v = c_ret_vector_128_u8();
    assert_or_panic(v[0] == 30);
    assert_or_panic(v[1] == 31);
    assert_or_panic(v[2] == 32);
    assert_or_panic(v[3] == 33);
    assert_or_panic(v[4] == 34);
    assert_or_panic(v[5] == 35);
    assert_or_panic(v[6] == 36);
    assert_or_panic(v[7] == 37);
    assert_or_panic(v[8] == 38);
    assert_or_panic(v[9] == 39);
    assert_or_panic(v[10] == 40);
    assert_or_panic(v[11] == 41);
    assert_or_panic(v[12] == 42);
    assert_or_panic(v[13] == 43);
    assert_or_panic(v[14] == 44);
    assert_or_panic(v[15] == 45);
    assert_or_panic(v[16] == 46);
    assert_or_panic(v[17] == 47);
    assert_or_panic(v[18] == 48);
    assert_or_panic(v[19] == 49);
    assert_or_panic(v[20] == 50);
    assert_or_panic(v[21] == 51);
    assert_or_panic(v[22] == 52);
    assert_or_panic(v[23] == 53);
    assert_or_panic(v[24] == 54);
    assert_or_panic(v[25] == 55);
    assert_or_panic(v[26] == 56);
    assert_or_panic(v[27] == 57);
    assert_or_panic(v[28] == 58);
    assert_or_panic(v[29] == 59);
    assert_or_panic(v[30] == 60);
    assert_or_panic(v[31] == 61);
    assert_or_panic(v[32] == 62);
    assert_or_panic(v[33] == 63);
    assert_or_panic(v[34] == 64);
    assert_or_panic(v[35] == 65);
    assert_or_panic(v[36] == 66);
    assert_or_panic(v[37] == 67);
    assert_or_panic(v[38] == 68);
    assert_or_panic(v[39] == 69);
    assert_or_panic(v[40] == 70);
    assert_or_panic(v[41] == 71);
    assert_or_panic(v[42] == 72);
    assert_or_panic(v[43] == 73);
    assert_or_panic(v[44] == 74);
    assert_or_panic(v[45] == 75);
    assert_or_panic(v[46] == 76);
    assert_or_panic(v[47] == 77);
    assert_or_panic(v[48] == 78);
    assert_or_panic(v[49] == 79);
    assert_or_panic(v[50] == 80);
    assert_or_panic(v[51] == 81);
    assert_or_panic(v[52] == 82);
    assert_or_panic(v[53] == 83);
    assert_or_panic(v[54] == 84);
    assert_or_panic(v[55] == 85);
    assert_or_panic(v[56] == 86);
    assert_or_panic(v[57] == 87);
    assert_or_panic(v[58] == 88);
    assert_or_panic(v[59] == 89);
    assert_or_panic(v[60] == 90);
    assert_or_panic(v[61] == 91);
    assert_or_panic(v[62] == 92);
    assert_or_panic(v[63] == 93);
    assert_or_panic(v[64] == 94);
    assert_or_panic(v[65] == 95);
    assert_or_panic(v[66] == 96);
    assert_or_panic(v[67] == 97);
    assert_or_panic(v[68] == 98);
    assert_or_panic(v[69] == 99);
    assert_or_panic(v[70] == 0);
    assert_or_panic(v[71] == 1);
    assert_or_panic(v[72] == 2);
    assert_or_panic(v[73] == 3);
    assert_or_panic(v[74] == 4);
    assert_or_panic(v[75] == 5);
    assert_or_panic(v[76] == 6);
    assert_or_panic(v[77] == 7);
    assert_or_panic(v[78] == 8);
    assert_or_panic(v[79] == 9);
    assert_or_panic(v[80] == 10);
    assert_or_panic(v[81] == 11);
    assert_or_panic(v[82] == 12);
    assert_or_panic(v[83] == 13);
    assert_or_panic(v[84] == 14);
    assert_or_panic(v[85] == 15);
    assert_or_panic(v[86] == 16);
    assert_or_panic(v[87] == 17);
    assert_or_panic(v[88] == 18);
    assert_or_panic(v[89] == 19);
    assert_or_panic(v[90] == 20);
    assert_or_panic(v[91] == 21);
    assert_or_panic(v[92] == 22);
    assert_or_panic(v[93] == 23);
    assert_or_panic(v[94] == 24);
    assert_or_panic(v[95] == 25);
    assert_or_panic(v[96] == 26);
    assert_or_panic(v[97] == 27);
    assert_or_panic(v[98] == 28);
    assert_or_panic(v[99] == 29);
    assert_or_panic(v[100] == 30);
    assert_or_panic(v[101] == 31);
    assert_or_panic(v[102] == 32);
    assert_or_panic(v[103] == 33);
    assert_or_panic(v[104] == 34);
    assert_or_panic(v[105] == 35);
    assert_or_panic(v[106] == 36);
    assert_or_panic(v[107] == 37);
    assert_or_panic(v[108] == 38);
    assert_or_panic(v[109] == 39);
    assert_or_panic(v[110] == 40);
    assert_or_panic(v[111] == 41);
    assert_or_panic(v[112] == 42);
    assert_or_panic(v[113] == 43);
    assert_or_panic(v[114] == 44);
    assert_or_panic(v[115] == 45);
    assert_or_panic(v[116] == 46);
    assert_or_panic(v[117] == 47);
    assert_or_panic(v[118] == 48);
    assert_or_panic(v[119] == 49);
    assert_or_panic(v[120] == 50);
    assert_or_panic(v[121] == 51);
    assert_or_panic(v[122] == 52);
    assert_or_panic(v[123] == 53);
    assert_or_panic(v[124] == 54);
    assert_or_panic(v[125] == 55);
    assert_or_panic(v[126] == 56);
    assert_or_panic(v[127] == 57);
    c_vector_128_u8((Vector_128_u8){
        58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
        74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
        90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
        6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
        38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
        70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
    }, 128);
    c_test_vector_128_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint8_t Vector_192_u8 __attribute__((vector_size(192 * sizeof(uint8_t))));
Vector_192_u8 zig_ret_vector_192_u8(void) {
    return (Vector_192_u8){
    86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
    2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
    98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
    46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
    };
}
void zig_vector_192_u8(Vector_192_u8 v, size_t i) {
    assert_or_panic(v[0] == 78);
    assert_or_panic(v[1] == 79);
    assert_or_panic(v[2] == 80);
    assert_or_panic(v[3] == 81);
    assert_or_panic(v[4] == 82);
    assert_or_panic(v[5] == 83);
    assert_or_panic(v[6] == 84);
    assert_or_panic(v[7] == 85);
    assert_or_panic(v[8] == 86);
    assert_or_panic(v[9] == 87);
    assert_or_panic(v[10] == 88);
    assert_or_panic(v[11] == 89);
    assert_or_panic(v[12] == 90);
    assert_or_panic(v[13] == 91);
    assert_or_panic(v[14] == 92);
    assert_or_panic(v[15] == 93);
    assert_or_panic(v[16] == 94);
    assert_or_panic(v[17] == 95);
    assert_or_panic(v[18] == 96);
    assert_or_panic(v[19] == 97);
    assert_or_panic(v[20] == 98);
    assert_or_panic(v[21] == 99);
    assert_or_panic(v[22] == 0);
    assert_or_panic(v[23] == 1);
    assert_or_panic(v[24] == 2);
    assert_or_panic(v[25] == 3);
    assert_or_panic(v[26] == 4);
    assert_or_panic(v[27] == 5);
    assert_or_panic(v[28] == 6);
    assert_or_panic(v[29] == 7);
    assert_or_panic(v[30] == 8);
    assert_or_panic(v[31] == 9);
    assert_or_panic(v[32] == 10);
    assert_or_panic(v[33] == 11);
    assert_or_panic(v[34] == 12);
    assert_or_panic(v[35] == 13);
    assert_or_panic(v[36] == 14);
    assert_or_panic(v[37] == 15);
    assert_or_panic(v[38] == 16);
    assert_or_panic(v[39] == 17);
    assert_or_panic(v[40] == 18);
    assert_or_panic(v[41] == 19);
    assert_or_panic(v[42] == 20);
    assert_or_panic(v[43] == 21);
    assert_or_panic(v[44] == 22);
    assert_or_panic(v[45] == 23);
    assert_or_panic(v[46] == 24);
    assert_or_panic(v[47] == 25);
    assert_or_panic(v[48] == 26);
    assert_or_panic(v[49] == 27);
    assert_or_panic(v[50] == 28);
    assert_or_panic(v[51] == 29);
    assert_or_panic(v[52] == 30);
    assert_or_panic(v[53] == 31);
    assert_or_panic(v[54] == 32);
    assert_or_panic(v[55] == 33);
    assert_or_panic(v[56] == 34);
    assert_or_panic(v[57] == 35);
    assert_or_panic(v[58] == 36);
    assert_or_panic(v[59] == 37);
    assert_or_panic(v[60] == 38);
    assert_or_panic(v[61] == 39);
    assert_or_panic(v[62] == 40);
    assert_or_panic(v[63] == 41);
    assert_or_panic(v[64] == 42);
    assert_or_panic(v[65] == 43);
    assert_or_panic(v[66] == 44);
    assert_or_panic(v[67] == 45);
    assert_or_panic(v[68] == 46);
    assert_or_panic(v[69] == 47);
    assert_or_panic(v[70] == 48);
    assert_or_panic(v[71] == 49);
    assert_or_panic(v[72] == 50);
    assert_or_panic(v[73] == 51);
    assert_or_panic(v[74] == 52);
    assert_or_panic(v[75] == 53);
    assert_or_panic(v[76] == 54);
    assert_or_panic(v[77] == 55);
    assert_or_panic(v[78] == 56);
    assert_or_panic(v[79] == 57);
    assert_or_panic(v[80] == 58);
    assert_or_panic(v[81] == 59);
    assert_or_panic(v[82] == 60);
    assert_or_panic(v[83] == 61);
    assert_or_panic(v[84] == 62);
    assert_or_panic(v[85] == 63);
    assert_or_panic(v[86] == 64);
    assert_or_panic(v[87] == 65);
    assert_or_panic(v[88] == 66);
    assert_or_panic(v[89] == 67);
    assert_or_panic(v[90] == 68);
    assert_or_panic(v[91] == 69);
    assert_or_panic(v[92] == 70);
    assert_or_panic(v[93] == 71);
    assert_or_panic(v[94] == 72);
    assert_or_panic(v[95] == 73);
    assert_or_panic(v[96] == 74);
    assert_or_panic(v[97] == 75);
    assert_or_panic(v[98] == 76);
    assert_or_panic(v[99] == 77);
    assert_or_panic(v[100] == 78);
    assert_or_panic(v[101] == 79);
    assert_or_panic(v[102] == 80);
    assert_or_panic(v[103] == 81);
    assert_or_panic(v[104] == 82);
    assert_or_panic(v[105] == 83);
    assert_or_panic(v[106] == 84);
    assert_or_panic(v[107] == 85);
    assert_or_panic(v[108] == 86);
    assert_or_panic(v[109] == 87);
    assert_or_panic(v[110] == 88);
    assert_or_panic(v[111] == 89);
    assert_or_panic(v[112] == 90);
    assert_or_panic(v[113] == 91);
    assert_or_panic(v[114] == 92);
    assert_or_panic(v[115] == 93);
    assert_or_panic(v[116] == 94);
    assert_or_panic(v[117] == 95);
    assert_or_panic(v[118] == 96);
    assert_or_panic(v[119] == 97);
    assert_or_panic(v[120] == 98);
    assert_or_panic(v[121] == 99);
    assert_or_panic(v[122] == 0);
    assert_or_panic(v[123] == 1);
    assert_or_panic(v[124] == 2);
    assert_or_panic(v[125] == 3);
    assert_or_panic(v[126] == 4);
    assert_or_panic(v[127] == 5);
    assert_or_panic(v[128] == 6);
    assert_or_panic(v[129] == 7);
    assert_or_panic(v[130] == 8);
    assert_or_panic(v[131] == 9);
    assert_or_panic(v[132] == 10);
    assert_or_panic(v[133] == 11);
    assert_or_panic(v[134] == 12);
    assert_or_panic(v[135] == 13);
    assert_or_panic(v[136] == 14);
    assert_or_panic(v[137] == 15);
    assert_or_panic(v[138] == 16);
    assert_or_panic(v[139] == 17);
    assert_or_panic(v[140] == 18);
    assert_or_panic(v[141] == 19);
    assert_or_panic(v[142] == 20);
    assert_or_panic(v[143] == 21);
    assert_or_panic(v[144] == 22);
    assert_or_panic(v[145] == 23);
    assert_or_panic(v[146] == 24);
    assert_or_panic(v[147] == 25);
    assert_or_panic(v[148] == 26);
    assert_or_panic(v[149] == 27);
    assert_or_panic(v[150] == 28);
    assert_or_panic(v[151] == 29);
    assert_or_panic(v[152] == 30);
    assert_or_panic(v[153] == 31);
    assert_or_panic(v[154] == 32);
    assert_or_panic(v[155] == 33);
    assert_or_panic(v[156] == 34);
    assert_or_panic(v[157] == 35);
    assert_or_panic(v[158] == 36);
    assert_or_panic(v[159] == 37);
    assert_or_panic(v[160] == 38);
    assert_or_panic(v[161] == 39);
    assert_or_panic(v[162] == 40);
    assert_or_panic(v[163] == 41);
    assert_or_panic(v[164] == 42);
    assert_or_panic(v[165] == 43);
    assert_or_panic(v[166] == 44);
    assert_or_panic(v[167] == 45);
    assert_or_panic(v[168] == 46);
    assert_or_panic(v[169] == 47);
    assert_or_panic(v[170] == 48);
    assert_or_panic(v[171] == 49);
    assert_or_panic(v[172] == 50);
    assert_or_panic(v[173] == 51);
    assert_or_panic(v[174] == 52);
    assert_or_panic(v[175] == 53);
    assert_or_panic(v[176] == 54);
    assert_or_panic(v[177] == 55);
    assert_or_panic(v[178] == 56);
    assert_or_panic(v[179] == 57);
    assert_or_panic(v[180] == 58);
    assert_or_panic(v[181] == 59);
    assert_or_panic(v[182] == 60);
    assert_or_panic(v[183] == 61);
    assert_or_panic(v[184] == 62);
    assert_or_panic(v[185] == 63);
    assert_or_panic(v[186] == 64);
    assert_or_panic(v[187] == 65);
    assert_or_panic(v[188] == 66);
    assert_or_panic(v[189] == 67);
    assert_or_panic(v[190] == 68);
    assert_or_panic(v[191] == 69);
    assert_or_panic(i == 192);
}
Vector_192_u8 c_ret_vector_192_u8(void);
void c_vector_192_u8(Vector_192_u8, size_t);
void c_test_vector_192_u8(void);
static void test_vector_192_u8(void) {
    c_abi_current_test = "@Vector(192, u8)";
    Vector_192_u8 v = c_ret_vector_192_u8();
    assert_or_panic(v[0] == 70);
    assert_or_panic(v[1] == 71);
    assert_or_panic(v[2] == 72);
    assert_or_panic(v[3] == 73);
    assert_or_panic(v[4] == 74);
    assert_or_panic(v[5] == 75);
    assert_or_panic(v[6] == 76);
    assert_or_panic(v[7] == 77);
    assert_or_panic(v[8] == 78);
    assert_or_panic(v[9] == 79);
    assert_or_panic(v[10] == 80);
    assert_or_panic(v[11] == 81);
    assert_or_panic(v[12] == 82);
    assert_or_panic(v[13] == 83);
    assert_or_panic(v[14] == 84);
    assert_or_panic(v[15] == 85);
    assert_or_panic(v[16] == 86);
    assert_or_panic(v[17] == 87);
    assert_or_panic(v[18] == 88);
    assert_or_panic(v[19] == 89);
    assert_or_panic(v[20] == 90);
    assert_or_panic(v[21] == 91);
    assert_or_panic(v[22] == 92);
    assert_or_panic(v[23] == 93);
    assert_or_panic(v[24] == 94);
    assert_or_panic(v[25] == 95);
    assert_or_panic(v[26] == 96);
    assert_or_panic(v[27] == 97);
    assert_or_panic(v[28] == 98);
    assert_or_panic(v[29] == 99);
    assert_or_panic(v[30] == 0);
    assert_or_panic(v[31] == 1);
    assert_or_panic(v[32] == 2);
    assert_or_panic(v[33] == 3);
    assert_or_panic(v[34] == 4);
    assert_or_panic(v[35] == 5);
    assert_or_panic(v[36] == 6);
    assert_or_panic(v[37] == 7);
    assert_or_panic(v[38] == 8);
    assert_or_panic(v[39] == 9);
    assert_or_panic(v[40] == 10);
    assert_or_panic(v[41] == 11);
    assert_or_panic(v[42] == 12);
    assert_or_panic(v[43] == 13);
    assert_or_panic(v[44] == 14);
    assert_or_panic(v[45] == 15);
    assert_or_panic(v[46] == 16);
    assert_or_panic(v[47] == 17);
    assert_or_panic(v[48] == 18);
    assert_or_panic(v[49] == 19);
    assert_or_panic(v[50] == 20);
    assert_or_panic(v[51] == 21);
    assert_or_panic(v[52] == 22);
    assert_or_panic(v[53] == 23);
    assert_or_panic(v[54] == 24);
    assert_or_panic(v[55] == 25);
    assert_or_panic(v[56] == 26);
    assert_or_panic(v[57] == 27);
    assert_or_panic(v[58] == 28);
    assert_or_panic(v[59] == 29);
    assert_or_panic(v[60] == 30);
    assert_or_panic(v[61] == 31);
    assert_or_panic(v[62] == 32);
    assert_or_panic(v[63] == 33);
    assert_or_panic(v[64] == 34);
    assert_or_panic(v[65] == 35);
    assert_or_panic(v[66] == 36);
    assert_or_panic(v[67] == 37);
    assert_or_panic(v[68] == 38);
    assert_or_panic(v[69] == 39);
    assert_or_panic(v[70] == 40);
    assert_or_panic(v[71] == 41);
    assert_or_panic(v[72] == 42);
    assert_or_panic(v[73] == 43);
    assert_or_panic(v[74] == 44);
    assert_or_panic(v[75] == 45);
    assert_or_panic(v[76] == 46);
    assert_or_panic(v[77] == 47);
    assert_or_panic(v[78] == 48);
    assert_or_panic(v[79] == 49);
    assert_or_panic(v[80] == 50);
    assert_or_panic(v[81] == 51);
    assert_or_panic(v[82] == 52);
    assert_or_panic(v[83] == 53);
    assert_or_panic(v[84] == 54);
    assert_or_panic(v[85] == 55);
    assert_or_panic(v[86] == 56);
    assert_or_panic(v[87] == 57);
    assert_or_panic(v[88] == 58);
    assert_or_panic(v[89] == 59);
    assert_or_panic(v[90] == 60);
    assert_or_panic(v[91] == 61);
    assert_or_panic(v[92] == 62);
    assert_or_panic(v[93] == 63);
    assert_or_panic(v[94] == 64);
    assert_or_panic(v[95] == 65);
    assert_or_panic(v[96] == 66);
    assert_or_panic(v[97] == 67);
    assert_or_panic(v[98] == 68);
    assert_or_panic(v[99] == 69);
    assert_or_panic(v[100] == 70);
    assert_or_panic(v[101] == 71);
    assert_or_panic(v[102] == 72);
    assert_or_panic(v[103] == 73);
    assert_or_panic(v[104] == 74);
    assert_or_panic(v[105] == 75);
    assert_or_panic(v[106] == 76);
    assert_or_panic(v[107] == 77);
    assert_or_panic(v[108] == 78);
    assert_or_panic(v[109] == 79);
    assert_or_panic(v[110] == 80);
    assert_or_panic(v[111] == 81);
    assert_or_panic(v[112] == 82);
    assert_or_panic(v[113] == 83);
    assert_or_panic(v[114] == 84);
    assert_or_panic(v[115] == 85);
    assert_or_panic(v[116] == 86);
    assert_or_panic(v[117] == 87);
    assert_or_panic(v[118] == 88);
    assert_or_panic(v[119] == 89);
    assert_or_panic(v[120] == 90);
    assert_or_panic(v[121] == 91);
    assert_or_panic(v[122] == 92);
    assert_or_panic(v[123] == 93);
    assert_or_panic(v[124] == 94);
    assert_or_panic(v[125] == 95);
    assert_or_panic(v[126] == 96);
    assert_or_panic(v[127] == 97);
    assert_or_panic(v[128] == 98);
    assert_or_panic(v[129] == 99);
    assert_or_panic(v[130] == 0);
    assert_or_panic(v[131] == 1);
    assert_or_panic(v[132] == 2);
    assert_or_panic(v[133] == 3);
    assert_or_panic(v[134] == 4);
    assert_or_panic(v[135] == 5);
    assert_or_panic(v[136] == 6);
    assert_or_panic(v[137] == 7);
    assert_or_panic(v[138] == 8);
    assert_or_panic(v[139] == 9);
    assert_or_panic(v[140] == 10);
    assert_or_panic(v[141] == 11);
    assert_or_panic(v[142] == 12);
    assert_or_panic(v[143] == 13);
    assert_or_panic(v[144] == 14);
    assert_or_panic(v[145] == 15);
    assert_or_panic(v[146] == 16);
    assert_or_panic(v[147] == 17);
    assert_or_panic(v[148] == 18);
    assert_or_panic(v[149] == 19);
    assert_or_panic(v[150] == 20);
    assert_or_panic(v[151] == 21);
    assert_or_panic(v[152] == 22);
    assert_or_panic(v[153] == 23);
    assert_or_panic(v[154] == 24);
    assert_or_panic(v[155] == 25);
    assert_or_panic(v[156] == 26);
    assert_or_panic(v[157] == 27);
    assert_or_panic(v[158] == 28);
    assert_or_panic(v[159] == 29);
    assert_or_panic(v[160] == 30);
    assert_or_panic(v[161] == 31);
    assert_or_panic(v[162] == 32);
    assert_or_panic(v[163] == 33);
    assert_or_panic(v[164] == 34);
    assert_or_panic(v[165] == 35);
    assert_or_panic(v[166] == 36);
    assert_or_panic(v[167] == 37);
    assert_or_panic(v[168] == 38);
    assert_or_panic(v[169] == 39);
    assert_or_panic(v[170] == 40);
    assert_or_panic(v[171] == 41);
    assert_or_panic(v[172] == 42);
    assert_or_panic(v[173] == 43);
    assert_or_panic(v[174] == 44);
    assert_or_panic(v[175] == 45);
    assert_or_panic(v[176] == 46);
    assert_or_panic(v[177] == 47);
    assert_or_panic(v[178] == 48);
    assert_or_panic(v[179] == 49);
    assert_or_panic(v[180] == 50);
    assert_or_panic(v[181] == 51);
    assert_or_panic(v[182] == 52);
    assert_or_panic(v[183] == 53);
    assert_or_panic(v[184] == 54);
    assert_or_panic(v[185] == 55);
    assert_or_panic(v[186] == 56);
    assert_or_panic(v[187] == 57);
    assert_or_panic(v[188] == 58);
    assert_or_panic(v[189] == 59);
    assert_or_panic(v[190] == 60);
    assert_or_panic(v[191] == 61);
    c_vector_192_u8((Vector_192_u8){
        62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
        78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
        94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
        42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
        58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
        74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
        90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
        6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
        38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
    }, 192);
    c_test_vector_192_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint8_t Vector_256_u8 __attribute__((vector_size(256 * sizeof(uint8_t))));
Vector_256_u8 zig_ret_vector_256_u8(void) {
    return (Vector_256_u8){
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
    86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
    2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
    98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
    46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
    78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
    94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    };
}
void zig_vector_256_u8(Vector_256_u8 v, size_t i) {
    assert_or_panic(v[0] == 10);
    assert_or_panic(v[1] == 11);
    assert_or_panic(v[2] == 12);
    assert_or_panic(v[3] == 13);
    assert_or_panic(v[4] == 14);
    assert_or_panic(v[5] == 15);
    assert_or_panic(v[6] == 16);
    assert_or_panic(v[7] == 17);
    assert_or_panic(v[8] == 18);
    assert_or_panic(v[9] == 19);
    assert_or_panic(v[10] == 20);
    assert_or_panic(v[11] == 21);
    assert_or_panic(v[12] == 22);
    assert_or_panic(v[13] == 23);
    assert_or_panic(v[14] == 24);
    assert_or_panic(v[15] == 25);
    assert_or_panic(v[16] == 26);
    assert_or_panic(v[17] == 27);
    assert_or_panic(v[18] == 28);
    assert_or_panic(v[19] == 29);
    assert_or_panic(v[20] == 30);
    assert_or_panic(v[21] == 31);
    assert_or_panic(v[22] == 32);
    assert_or_panic(v[23] == 33);
    assert_or_panic(v[24] == 34);
    assert_or_panic(v[25] == 35);
    assert_or_panic(v[26] == 36);
    assert_or_panic(v[27] == 37);
    assert_or_panic(v[28] == 38);
    assert_or_panic(v[29] == 39);
    assert_or_panic(v[30] == 40);
    assert_or_panic(v[31] == 41);
    assert_or_panic(v[32] == 42);
    assert_or_panic(v[33] == 43);
    assert_or_panic(v[34] == 44);
    assert_or_panic(v[35] == 45);
    assert_or_panic(v[36] == 46);
    assert_or_panic(v[37] == 47);
    assert_or_panic(v[38] == 48);
    assert_or_panic(v[39] == 49);
    assert_or_panic(v[40] == 50);
    assert_or_panic(v[41] == 51);
    assert_or_panic(v[42] == 52);
    assert_or_panic(v[43] == 53);
    assert_or_panic(v[44] == 54);
    assert_or_panic(v[45] == 55);
    assert_or_panic(v[46] == 56);
    assert_or_panic(v[47] == 57);
    assert_or_panic(v[48] == 58);
    assert_or_panic(v[49] == 59);
    assert_or_panic(v[50] == 60);
    assert_or_panic(v[51] == 61);
    assert_or_panic(v[52] == 62);
    assert_or_panic(v[53] == 63);
    assert_or_panic(v[54] == 64);
    assert_or_panic(v[55] == 65);
    assert_or_panic(v[56] == 66);
    assert_or_panic(v[57] == 67);
    assert_or_panic(v[58] == 68);
    assert_or_panic(v[59] == 69);
    assert_or_panic(v[60] == 70);
    assert_or_panic(v[61] == 71);
    assert_or_panic(v[62] == 72);
    assert_or_panic(v[63] == 73);
    assert_or_panic(v[64] == 74);
    assert_or_panic(v[65] == 75);
    assert_or_panic(v[66] == 76);
    assert_or_panic(v[67] == 77);
    assert_or_panic(v[68] == 78);
    assert_or_panic(v[69] == 79);
    assert_or_panic(v[70] == 80);
    assert_or_panic(v[71] == 81);
    assert_or_panic(v[72] == 82);
    assert_or_panic(v[73] == 83);
    assert_or_panic(v[74] == 84);
    assert_or_panic(v[75] == 85);
    assert_or_panic(v[76] == 86);
    assert_or_panic(v[77] == 87);
    assert_or_panic(v[78] == 88);
    assert_or_panic(v[79] == 89);
    assert_or_panic(v[80] == 90);
    assert_or_panic(v[81] == 91);
    assert_or_panic(v[82] == 92);
    assert_or_panic(v[83] == 93);
    assert_or_panic(v[84] == 94);
    assert_or_panic(v[85] == 95);
    assert_or_panic(v[86] == 96);
    assert_or_panic(v[87] == 97);
    assert_or_panic(v[88] == 98);
    assert_or_panic(v[89] == 99);
    assert_or_panic(v[90] == 0);
    assert_or_panic(v[91] == 1);
    assert_or_panic(v[92] == 2);
    assert_or_panic(v[93] == 3);
    assert_or_panic(v[94] == 4);
    assert_or_panic(v[95] == 5);
    assert_or_panic(v[96] == 6);
    assert_or_panic(v[97] == 7);
    assert_or_panic(v[98] == 8);
    assert_or_panic(v[99] == 9);
    assert_or_panic(v[100] == 10);
    assert_or_panic(v[101] == 11);
    assert_or_panic(v[102] == 12);
    assert_or_panic(v[103] == 13);
    assert_or_panic(v[104] == 14);
    assert_or_panic(v[105] == 15);
    assert_or_panic(v[106] == 16);
    assert_or_panic(v[107] == 17);
    assert_or_panic(v[108] == 18);
    assert_or_panic(v[109] == 19);
    assert_or_panic(v[110] == 20);
    assert_or_panic(v[111] == 21);
    assert_or_panic(v[112] == 22);
    assert_or_panic(v[113] == 23);
    assert_or_panic(v[114] == 24);
    assert_or_panic(v[115] == 25);
    assert_or_panic(v[116] == 26);
    assert_or_panic(v[117] == 27);
    assert_or_panic(v[118] == 28);
    assert_or_panic(v[119] == 29);
    assert_or_panic(v[120] == 30);
    assert_or_panic(v[121] == 31);
    assert_or_panic(v[122] == 32);
    assert_or_panic(v[123] == 33);
    assert_or_panic(v[124] == 34);
    assert_or_panic(v[125] == 35);
    assert_or_panic(v[126] == 36);
    assert_or_panic(v[127] == 37);
    assert_or_panic(v[128] == 38);
    assert_or_panic(v[129] == 39);
    assert_or_panic(v[130] == 40);
    assert_or_panic(v[131] == 41);
    assert_or_panic(v[132] == 42);
    assert_or_panic(v[133] == 43);
    assert_or_panic(v[134] == 44);
    assert_or_panic(v[135] == 45);
    assert_or_panic(v[136] == 46);
    assert_or_panic(v[137] == 47);
    assert_or_panic(v[138] == 48);
    assert_or_panic(v[139] == 49);
    assert_or_panic(v[140] == 50);
    assert_or_panic(v[141] == 51);
    assert_or_panic(v[142] == 52);
    assert_or_panic(v[143] == 53);
    assert_or_panic(v[144] == 54);
    assert_or_panic(v[145] == 55);
    assert_or_panic(v[146] == 56);
    assert_or_panic(v[147] == 57);
    assert_or_panic(v[148] == 58);
    assert_or_panic(v[149] == 59);
    assert_or_panic(v[150] == 60);
    assert_or_panic(v[151] == 61);
    assert_or_panic(v[152] == 62);
    assert_or_panic(v[153] == 63);
    assert_or_panic(v[154] == 64);
    assert_or_panic(v[155] == 65);
    assert_or_panic(v[156] == 66);
    assert_or_panic(v[157] == 67);
    assert_or_panic(v[158] == 68);
    assert_or_panic(v[159] == 69);
    assert_or_panic(v[160] == 70);
    assert_or_panic(v[161] == 71);
    assert_or_panic(v[162] == 72);
    assert_or_panic(v[163] == 73);
    assert_or_panic(v[164] == 74);
    assert_or_panic(v[165] == 75);
    assert_or_panic(v[166] == 76);
    assert_or_panic(v[167] == 77);
    assert_or_panic(v[168] == 78);
    assert_or_panic(v[169] == 79);
    assert_or_panic(v[170] == 80);
    assert_or_panic(v[171] == 81);
    assert_or_panic(v[172] == 82);
    assert_or_panic(v[173] == 83);
    assert_or_panic(v[174] == 84);
    assert_or_panic(v[175] == 85);
    assert_or_panic(v[176] == 86);
    assert_or_panic(v[177] == 87);
    assert_or_panic(v[178] == 88);
    assert_or_panic(v[179] == 89);
    assert_or_panic(v[180] == 90);
    assert_or_panic(v[181] == 91);
    assert_or_panic(v[182] == 92);
    assert_or_panic(v[183] == 93);
    assert_or_panic(v[184] == 94);
    assert_or_panic(v[185] == 95);
    assert_or_panic(v[186] == 96);
    assert_or_panic(v[187] == 97);
    assert_or_panic(v[188] == 98);
    assert_or_panic(v[189] == 99);
    assert_or_panic(v[190] == 0);
    assert_or_panic(v[191] == 1);
    assert_or_panic(v[192] == 2);
    assert_or_panic(v[193] == 3);
    assert_or_panic(v[194] == 4);
    assert_or_panic(v[195] == 5);
    assert_or_panic(v[196] == 6);
    assert_or_panic(v[197] == 7);
    assert_or_panic(v[198] == 8);
    assert_or_panic(v[199] == 9);
    assert_or_panic(v[200] == 10);
    assert_or_panic(v[201] == 11);
    assert_or_panic(v[202] == 12);
    assert_or_panic(v[203] == 13);
    assert_or_panic(v[204] == 14);
    assert_or_panic(v[205] == 15);
    assert_or_panic(v[206] == 16);
    assert_or_panic(v[207] == 17);
    assert_or_panic(v[208] == 18);
    assert_or_panic(v[209] == 19);
    assert_or_panic(v[210] == 20);
    assert_or_panic(v[211] == 21);
    assert_or_panic(v[212] == 22);
    assert_or_panic(v[213] == 23);
    assert_or_panic(v[214] == 24);
    assert_or_panic(v[215] == 25);
    assert_or_panic(v[216] == 26);
    assert_or_panic(v[217] == 27);
    assert_or_panic(v[218] == 28);
    assert_or_panic(v[219] == 29);
    assert_or_panic(v[220] == 30);
    assert_or_panic(v[221] == 31);
    assert_or_panic(v[222] == 32);
    assert_or_panic(v[223] == 33);
    assert_or_panic(v[224] == 34);
    assert_or_panic(v[225] == 35);
    assert_or_panic(v[226] == 36);
    assert_or_panic(v[227] == 37);
    assert_or_panic(v[228] == 38);
    assert_or_panic(v[229] == 39);
    assert_or_panic(v[230] == 40);
    assert_or_panic(v[231] == 41);
    assert_or_panic(v[232] == 42);
    assert_or_panic(v[233] == 43);
    assert_or_panic(v[234] == 44);
    assert_or_panic(v[235] == 45);
    assert_or_panic(v[236] == 46);
    assert_or_panic(v[237] == 47);
    assert_or_panic(v[238] == 48);
    assert_or_panic(v[239] == 49);
    assert_or_panic(v[240] == 50);
    assert_or_panic(v[241] == 51);
    assert_or_panic(v[242] == 52);
    assert_or_panic(v[243] == 53);
    assert_or_panic(v[244] == 54);
    assert_or_panic(v[245] == 55);
    assert_or_panic(v[246] == 56);
    assert_or_panic(v[247] == 57);
    assert_or_panic(v[248] == 58);
    assert_or_panic(v[249] == 59);
    assert_or_panic(v[250] == 60);
    assert_or_panic(v[251] == 61);
    assert_or_panic(v[252] == 62);
    assert_or_panic(v[253] == 63);
    assert_or_panic(v[254] == 64);
    assert_or_panic(v[255] == 65);
    assert_or_panic(i == 256);
}
Vector_256_u8 c_ret_vector_256_u8(void);
void c_vector_256_u8(Vector_256_u8, size_t);
void c_test_vector_256_u8(void);
static void test_vector_256_u8(void) {
    c_abi_current_test = "@Vector(256, u8)";
    Vector_256_u8 v = c_ret_vector_256_u8();
    assert_or_panic(v[0] == 66);
    assert_or_panic(v[1] == 67);
    assert_or_panic(v[2] == 68);
    assert_or_panic(v[3] == 69);
    assert_or_panic(v[4] == 70);
    assert_or_panic(v[5] == 71);
    assert_or_panic(v[6] == 72);
    assert_or_panic(v[7] == 73);
    assert_or_panic(v[8] == 74);
    assert_or_panic(v[9] == 75);
    assert_or_panic(v[10] == 76);
    assert_or_panic(v[11] == 77);
    assert_or_panic(v[12] == 78);
    assert_or_panic(v[13] == 79);
    assert_or_panic(v[14] == 80);
    assert_or_panic(v[15] == 81);
    assert_or_panic(v[16] == 82);
    assert_or_panic(v[17] == 83);
    assert_or_panic(v[18] == 84);
    assert_or_panic(v[19] == 85);
    assert_or_panic(v[20] == 86);
    assert_or_panic(v[21] == 87);
    assert_or_panic(v[22] == 88);
    assert_or_panic(v[23] == 89);
    assert_or_panic(v[24] == 90);
    assert_or_panic(v[25] == 91);
    assert_or_panic(v[26] == 92);
    assert_or_panic(v[27] == 93);
    assert_or_panic(v[28] == 94);
    assert_or_panic(v[29] == 95);
    assert_or_panic(v[30] == 96);
    assert_or_panic(v[31] == 97);
    assert_or_panic(v[32] == 98);
    assert_or_panic(v[33] == 99);
    assert_or_panic(v[34] == 0);
    assert_or_panic(v[35] == 1);
    assert_or_panic(v[36] == 2);
    assert_or_panic(v[37] == 3);
    assert_or_panic(v[38] == 4);
    assert_or_panic(v[39] == 5);
    assert_or_panic(v[40] == 6);
    assert_or_panic(v[41] == 7);
    assert_or_panic(v[42] == 8);
    assert_or_panic(v[43] == 9);
    assert_or_panic(v[44] == 10);
    assert_or_panic(v[45] == 11);
    assert_or_panic(v[46] == 12);
    assert_or_panic(v[47] == 13);
    assert_or_panic(v[48] == 14);
    assert_or_panic(v[49] == 15);
    assert_or_panic(v[50] == 16);
    assert_or_panic(v[51] == 17);
    assert_or_panic(v[52] == 18);
    assert_or_panic(v[53] == 19);
    assert_or_panic(v[54] == 20);
    assert_or_panic(v[55] == 21);
    assert_or_panic(v[56] == 22);
    assert_or_panic(v[57] == 23);
    assert_or_panic(v[58] == 24);
    assert_or_panic(v[59] == 25);
    assert_or_panic(v[60] == 26);
    assert_or_panic(v[61] == 27);
    assert_or_panic(v[62] == 28);
    assert_or_panic(v[63] == 29);
    assert_or_panic(v[64] == 30);
    assert_or_panic(v[65] == 31);
    assert_or_panic(v[66] == 32);
    assert_or_panic(v[67] == 33);
    assert_or_panic(v[68] == 34);
    assert_or_panic(v[69] == 35);
    assert_or_panic(v[70] == 36);
    assert_or_panic(v[71] == 37);
    assert_or_panic(v[72] == 38);
    assert_or_panic(v[73] == 39);
    assert_or_panic(v[74] == 40);
    assert_or_panic(v[75] == 41);
    assert_or_panic(v[76] == 42);
    assert_or_panic(v[77] == 43);
    assert_or_panic(v[78] == 44);
    assert_or_panic(v[79] == 45);
    assert_or_panic(v[80] == 46);
    assert_or_panic(v[81] == 47);
    assert_or_panic(v[82] == 48);
    assert_or_panic(v[83] == 49);
    assert_or_panic(v[84] == 50);
    assert_or_panic(v[85] == 51);
    assert_or_panic(v[86] == 52);
    assert_or_panic(v[87] == 53);
    assert_or_panic(v[88] == 54);
    assert_or_panic(v[89] == 55);
    assert_or_panic(v[90] == 56);
    assert_or_panic(v[91] == 57);
    assert_or_panic(v[92] == 58);
    assert_or_panic(v[93] == 59);
    assert_or_panic(v[94] == 60);
    assert_or_panic(v[95] == 61);
    assert_or_panic(v[96] == 62);
    assert_or_panic(v[97] == 63);
    assert_or_panic(v[98] == 64);
    assert_or_panic(v[99] == 65);
    assert_or_panic(v[100] == 66);
    assert_or_panic(v[101] == 67);
    assert_or_panic(v[102] == 68);
    assert_or_panic(v[103] == 69);
    assert_or_panic(v[104] == 70);
    assert_or_panic(v[105] == 71);
    assert_or_panic(v[106] == 72);
    assert_or_panic(v[107] == 73);
    assert_or_panic(v[108] == 74);
    assert_or_panic(v[109] == 75);
    assert_or_panic(v[110] == 76);
    assert_or_panic(v[111] == 77);
    assert_or_panic(v[112] == 78);
    assert_or_panic(v[113] == 79);
    assert_or_panic(v[114] == 80);
    assert_or_panic(v[115] == 81);
    assert_or_panic(v[116] == 82);
    assert_or_panic(v[117] == 83);
    assert_or_panic(v[118] == 84);
    assert_or_panic(v[119] == 85);
    assert_or_panic(v[120] == 86);
    assert_or_panic(v[121] == 87);
    assert_or_panic(v[122] == 88);
    assert_or_panic(v[123] == 89);
    assert_or_panic(v[124] == 90);
    assert_or_panic(v[125] == 91);
    assert_or_panic(v[126] == 92);
    assert_or_panic(v[127] == 93);
    assert_or_panic(v[128] == 94);
    assert_or_panic(v[129] == 95);
    assert_or_panic(v[130] == 96);
    assert_or_panic(v[131] == 97);
    assert_or_panic(v[132] == 98);
    assert_or_panic(v[133] == 99);
    assert_or_panic(v[134] == 0);
    assert_or_panic(v[135] == 1);
    assert_or_panic(v[136] == 2);
    assert_or_panic(v[137] == 3);
    assert_or_panic(v[138] == 4);
    assert_or_panic(v[139] == 5);
    assert_or_panic(v[140] == 6);
    assert_or_panic(v[141] == 7);
    assert_or_panic(v[142] == 8);
    assert_or_panic(v[143] == 9);
    assert_or_panic(v[144] == 10);
    assert_or_panic(v[145] == 11);
    assert_or_panic(v[146] == 12);
    assert_or_panic(v[147] == 13);
    assert_or_panic(v[148] == 14);
    assert_or_panic(v[149] == 15);
    assert_or_panic(v[150] == 16);
    assert_or_panic(v[151] == 17);
    assert_or_panic(v[152] == 18);
    assert_or_panic(v[153] == 19);
    assert_or_panic(v[154] == 20);
    assert_or_panic(v[155] == 21);
    assert_or_panic(v[156] == 22);
    assert_or_panic(v[157] == 23);
    assert_or_panic(v[158] == 24);
    assert_or_panic(v[159] == 25);
    assert_or_panic(v[160] == 26);
    assert_or_panic(v[161] == 27);
    assert_or_panic(v[162] == 28);
    assert_or_panic(v[163] == 29);
    assert_or_panic(v[164] == 30);
    assert_or_panic(v[165] == 31);
    assert_or_panic(v[166] == 32);
    assert_or_panic(v[167] == 33);
    assert_or_panic(v[168] == 34);
    assert_or_panic(v[169] == 35);
    assert_or_panic(v[170] == 36);
    assert_or_panic(v[171] == 37);
    assert_or_panic(v[172] == 38);
    assert_or_panic(v[173] == 39);
    assert_or_panic(v[174] == 40);
    assert_or_panic(v[175] == 41);
    assert_or_panic(v[176] == 42);
    assert_or_panic(v[177] == 43);
    assert_or_panic(v[178] == 44);
    assert_or_panic(v[179] == 45);
    assert_or_panic(v[180] == 46);
    assert_or_panic(v[181] == 47);
    assert_or_panic(v[182] == 48);
    assert_or_panic(v[183] == 49);
    assert_or_panic(v[184] == 50);
    assert_or_panic(v[185] == 51);
    assert_or_panic(v[186] == 52);
    assert_or_panic(v[187] == 53);
    assert_or_panic(v[188] == 54);
    assert_or_panic(v[189] == 55);
    assert_or_panic(v[190] == 56);
    assert_or_panic(v[191] == 57);
    assert_or_panic(v[192] == 58);
    assert_or_panic(v[193] == 59);
    assert_or_panic(v[194] == 60);
    assert_or_panic(v[195] == 61);
    assert_or_panic(v[196] == 62);
    assert_or_panic(v[197] == 63);
    assert_or_panic(v[198] == 64);
    assert_or_panic(v[199] == 65);
    assert_or_panic(v[200] == 66);
    assert_or_panic(v[201] == 67);
    assert_or_panic(v[202] == 68);
    assert_or_panic(v[203] == 69);
    assert_or_panic(v[204] == 70);
    assert_or_panic(v[205] == 71);
    assert_or_panic(v[206] == 72);
    assert_or_panic(v[207] == 73);
    assert_or_panic(v[208] == 74);
    assert_or_panic(v[209] == 75);
    assert_or_panic(v[210] == 76);
    assert_or_panic(v[211] == 77);
    assert_or_panic(v[212] == 78);
    assert_or_panic(v[213] == 79);
    assert_or_panic(v[214] == 80);
    assert_or_panic(v[215] == 81);
    assert_or_panic(v[216] == 82);
    assert_or_panic(v[217] == 83);
    assert_or_panic(v[218] == 84);
    assert_or_panic(v[219] == 85);
    assert_or_panic(v[220] == 86);
    assert_or_panic(v[221] == 87);
    assert_or_panic(v[222] == 88);
    assert_or_panic(v[223] == 89);
    assert_or_panic(v[224] == 90);
    assert_or_panic(v[225] == 91);
    assert_or_panic(v[226] == 92);
    assert_or_panic(v[227] == 93);
    assert_or_panic(v[228] == 94);
    assert_or_panic(v[229] == 95);
    assert_or_panic(v[230] == 96);
    assert_or_panic(v[231] == 97);
    assert_or_panic(v[232] == 98);
    assert_or_panic(v[233] == 99);
    assert_or_panic(v[234] == 0);
    assert_or_panic(v[235] == 1);
    assert_or_panic(v[236] == 2);
    assert_or_panic(v[237] == 3);
    assert_or_panic(v[238] == 4);
    assert_or_panic(v[239] == 5);
    assert_or_panic(v[240] == 6);
    assert_or_panic(v[241] == 7);
    assert_or_panic(v[242] == 8);
    assert_or_panic(v[243] == 9);
    assert_or_panic(v[244] == 10);
    assert_or_panic(v[245] == 11);
    assert_or_panic(v[246] == 12);
    assert_or_panic(v[247] == 13);
    assert_or_panic(v[248] == 14);
    assert_or_panic(v[249] == 15);
    assert_or_panic(v[250] == 16);
    assert_or_panic(v[251] == 17);
    assert_or_panic(v[252] == 18);
    assert_or_panic(v[253] == 19);
    assert_or_panic(v[254] == 20);
    assert_or_panic(v[255] == 21);
    c_vector_256_u8((Vector_256_u8){
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
        38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
        70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
        86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
        2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
        50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
        66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
        82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
        98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
        14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
        30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
        46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
        62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
    }, 256);
    c_test_vector_256_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint8_t Vector_384_u8 __attribute__((vector_size(384 * sizeof(uint8_t))));
Vector_384_u8 zig_ret_vector_384_u8(void) {
    return (Vector_384_u8){
    78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
    94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
    42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
    74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
    6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
    86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
    2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
    98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
    46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    };
}
void zig_vector_384_u8(Vector_384_u8 v, size_t i) {
    assert_or_panic(v[0] == 62);
    assert_or_panic(v[1] == 63);
    assert_or_panic(v[2] == 64);
    assert_or_panic(v[3] == 65);
    assert_or_panic(v[4] == 66);
    assert_or_panic(v[5] == 67);
    assert_or_panic(v[6] == 68);
    assert_or_panic(v[7] == 69);
    assert_or_panic(v[8] == 70);
    assert_or_panic(v[9] == 71);
    assert_or_panic(v[10] == 72);
    assert_or_panic(v[11] == 73);
    assert_or_panic(v[12] == 74);
    assert_or_panic(v[13] == 75);
    assert_or_panic(v[14] == 76);
    assert_or_panic(v[15] == 77);
    assert_or_panic(v[16] == 78);
    assert_or_panic(v[17] == 79);
    assert_or_panic(v[18] == 80);
    assert_or_panic(v[19] == 81);
    assert_or_panic(v[20] == 82);
    assert_or_panic(v[21] == 83);
    assert_or_panic(v[22] == 84);
    assert_or_panic(v[23] == 85);
    assert_or_panic(v[24] == 86);
    assert_or_panic(v[25] == 87);
    assert_or_panic(v[26] == 88);
    assert_or_panic(v[27] == 89);
    assert_or_panic(v[28] == 90);
    assert_or_panic(v[29] == 91);
    assert_or_panic(v[30] == 92);
    assert_or_panic(v[31] == 93);
    assert_or_panic(v[32] == 94);
    assert_or_panic(v[33] == 95);
    assert_or_panic(v[34] == 96);
    assert_or_panic(v[35] == 97);
    assert_or_panic(v[36] == 98);
    assert_or_panic(v[37] == 99);
    assert_or_panic(v[38] == 0);
    assert_or_panic(v[39] == 1);
    assert_or_panic(v[40] == 2);
    assert_or_panic(v[41] == 3);
    assert_or_panic(v[42] == 4);
    assert_or_panic(v[43] == 5);
    assert_or_panic(v[44] == 6);
    assert_or_panic(v[45] == 7);
    assert_or_panic(v[46] == 8);
    assert_or_panic(v[47] == 9);
    assert_or_panic(v[48] == 10);
    assert_or_panic(v[49] == 11);
    assert_or_panic(v[50] == 12);
    assert_or_panic(v[51] == 13);
    assert_or_panic(v[52] == 14);
    assert_or_panic(v[53] == 15);
    assert_or_panic(v[54] == 16);
    assert_or_panic(v[55] == 17);
    assert_or_panic(v[56] == 18);
    assert_or_panic(v[57] == 19);
    assert_or_panic(v[58] == 20);
    assert_or_panic(v[59] == 21);
    assert_or_panic(v[60] == 22);
    assert_or_panic(v[61] == 23);
    assert_or_panic(v[62] == 24);
    assert_or_panic(v[63] == 25);
    assert_or_panic(v[64] == 26);
    assert_or_panic(v[65] == 27);
    assert_or_panic(v[66] == 28);
    assert_or_panic(v[67] == 29);
    assert_or_panic(v[68] == 30);
    assert_or_panic(v[69] == 31);
    assert_or_panic(v[70] == 32);
    assert_or_panic(v[71] == 33);
    assert_or_panic(v[72] == 34);
    assert_or_panic(v[73] == 35);
    assert_or_panic(v[74] == 36);
    assert_or_panic(v[75] == 37);
    assert_or_panic(v[76] == 38);
    assert_or_panic(v[77] == 39);
    assert_or_panic(v[78] == 40);
    assert_or_panic(v[79] == 41);
    assert_or_panic(v[80] == 42);
    assert_or_panic(v[81] == 43);
    assert_or_panic(v[82] == 44);
    assert_or_panic(v[83] == 45);
    assert_or_panic(v[84] == 46);
    assert_or_panic(v[85] == 47);
    assert_or_panic(v[86] == 48);
    assert_or_panic(v[87] == 49);
    assert_or_panic(v[88] == 50);
    assert_or_panic(v[89] == 51);
    assert_or_panic(v[90] == 52);
    assert_or_panic(v[91] == 53);
    assert_or_panic(v[92] == 54);
    assert_or_panic(v[93] == 55);
    assert_or_panic(v[94] == 56);
    assert_or_panic(v[95] == 57);
    assert_or_panic(v[96] == 58);
    assert_or_panic(v[97] == 59);
    assert_or_panic(v[98] == 60);
    assert_or_panic(v[99] == 61);
    assert_or_panic(v[100] == 62);
    assert_or_panic(v[101] == 63);
    assert_or_panic(v[102] == 64);
    assert_or_panic(v[103] == 65);
    assert_or_panic(v[104] == 66);
    assert_or_panic(v[105] == 67);
    assert_or_panic(v[106] == 68);
    assert_or_panic(v[107] == 69);
    assert_or_panic(v[108] == 70);
    assert_or_panic(v[109] == 71);
    assert_or_panic(v[110] == 72);
    assert_or_panic(v[111] == 73);
    assert_or_panic(v[112] == 74);
    assert_or_panic(v[113] == 75);
    assert_or_panic(v[114] == 76);
    assert_or_panic(v[115] == 77);
    assert_or_panic(v[116] == 78);
    assert_or_panic(v[117] == 79);
    assert_or_panic(v[118] == 80);
    assert_or_panic(v[119] == 81);
    assert_or_panic(v[120] == 82);
    assert_or_panic(v[121] == 83);
    assert_or_panic(v[122] == 84);
    assert_or_panic(v[123] == 85);
    assert_or_panic(v[124] == 86);
    assert_or_panic(v[125] == 87);
    assert_or_panic(v[126] == 88);
    assert_or_panic(v[127] == 89);
    assert_or_panic(v[128] == 90);
    assert_or_panic(v[129] == 91);
    assert_or_panic(v[130] == 92);
    assert_or_panic(v[131] == 93);
    assert_or_panic(v[132] == 94);
    assert_or_panic(v[133] == 95);
    assert_or_panic(v[134] == 96);
    assert_or_panic(v[135] == 97);
    assert_or_panic(v[136] == 98);
    assert_or_panic(v[137] == 99);
    assert_or_panic(v[138] == 0);
    assert_or_panic(v[139] == 1);
    assert_or_panic(v[140] == 2);
    assert_or_panic(v[141] == 3);
    assert_or_panic(v[142] == 4);
    assert_or_panic(v[143] == 5);
    assert_or_panic(v[144] == 6);
    assert_or_panic(v[145] == 7);
    assert_or_panic(v[146] == 8);
    assert_or_panic(v[147] == 9);
    assert_or_panic(v[148] == 10);
    assert_or_panic(v[149] == 11);
    assert_or_panic(v[150] == 12);
    assert_or_panic(v[151] == 13);
    assert_or_panic(v[152] == 14);
    assert_or_panic(v[153] == 15);
    assert_or_panic(v[154] == 16);
    assert_or_panic(v[155] == 17);
    assert_or_panic(v[156] == 18);
    assert_or_panic(v[157] == 19);
    assert_or_panic(v[158] == 20);
    assert_or_panic(v[159] == 21);
    assert_or_panic(v[160] == 22);
    assert_or_panic(v[161] == 23);
    assert_or_panic(v[162] == 24);
    assert_or_panic(v[163] == 25);
    assert_or_panic(v[164] == 26);
    assert_or_panic(v[165] == 27);
    assert_or_panic(v[166] == 28);
    assert_or_panic(v[167] == 29);
    assert_or_panic(v[168] == 30);
    assert_or_panic(v[169] == 31);
    assert_or_panic(v[170] == 32);
    assert_or_panic(v[171] == 33);
    assert_or_panic(v[172] == 34);
    assert_or_panic(v[173] == 35);
    assert_or_panic(v[174] == 36);
    assert_or_panic(v[175] == 37);
    assert_or_panic(v[176] == 38);
    assert_or_panic(v[177] == 39);
    assert_or_panic(v[178] == 40);
    assert_or_panic(v[179] == 41);
    assert_or_panic(v[180] == 42);
    assert_or_panic(v[181] == 43);
    assert_or_panic(v[182] == 44);
    assert_or_panic(v[183] == 45);
    assert_or_panic(v[184] == 46);
    assert_or_panic(v[185] == 47);
    assert_or_panic(v[186] == 48);
    assert_or_panic(v[187] == 49);
    assert_or_panic(v[188] == 50);
    assert_or_panic(v[189] == 51);
    assert_or_panic(v[190] == 52);
    assert_or_panic(v[191] == 53);
    assert_or_panic(v[192] == 54);
    assert_or_panic(v[193] == 55);
    assert_or_panic(v[194] == 56);
    assert_or_panic(v[195] == 57);
    assert_or_panic(v[196] == 58);
    assert_or_panic(v[197] == 59);
    assert_or_panic(v[198] == 60);
    assert_or_panic(v[199] == 61);
    assert_or_panic(v[200] == 62);
    assert_or_panic(v[201] == 63);
    assert_or_panic(v[202] == 64);
    assert_or_panic(v[203] == 65);
    assert_or_panic(v[204] == 66);
    assert_or_panic(v[205] == 67);
    assert_or_panic(v[206] == 68);
    assert_or_panic(v[207] == 69);
    assert_or_panic(v[208] == 70);
    assert_or_panic(v[209] == 71);
    assert_or_panic(v[210] == 72);
    assert_or_panic(v[211] == 73);
    assert_or_panic(v[212] == 74);
    assert_or_panic(v[213] == 75);
    assert_or_panic(v[214] == 76);
    assert_or_panic(v[215] == 77);
    assert_or_panic(v[216] == 78);
    assert_or_panic(v[217] == 79);
    assert_or_panic(v[218] == 80);
    assert_or_panic(v[219] == 81);
    assert_or_panic(v[220] == 82);
    assert_or_panic(v[221] == 83);
    assert_or_panic(v[222] == 84);
    assert_or_panic(v[223] == 85);
    assert_or_panic(v[224] == 86);
    assert_or_panic(v[225] == 87);
    assert_or_panic(v[226] == 88);
    assert_or_panic(v[227] == 89);
    assert_or_panic(v[228] == 90);
    assert_or_panic(v[229] == 91);
    assert_or_panic(v[230] == 92);
    assert_or_panic(v[231] == 93);
    assert_or_panic(v[232] == 94);
    assert_or_panic(v[233] == 95);
    assert_or_panic(v[234] == 96);
    assert_or_panic(v[235] == 97);
    assert_or_panic(v[236] == 98);
    assert_or_panic(v[237] == 99);
    assert_or_panic(v[238] == 0);
    assert_or_panic(v[239] == 1);
    assert_or_panic(v[240] == 2);
    assert_or_panic(v[241] == 3);
    assert_or_panic(v[242] == 4);
    assert_or_panic(v[243] == 5);
    assert_or_panic(v[244] == 6);
    assert_or_panic(v[245] == 7);
    assert_or_panic(v[246] == 8);
    assert_or_panic(v[247] == 9);
    assert_or_panic(v[248] == 10);
    assert_or_panic(v[249] == 11);
    assert_or_panic(v[250] == 12);
    assert_or_panic(v[251] == 13);
    assert_or_panic(v[252] == 14);
    assert_or_panic(v[253] == 15);
    assert_or_panic(v[254] == 16);
    assert_or_panic(v[255] == 17);
    assert_or_panic(v[256] == 18);
    assert_or_panic(v[257] == 19);
    assert_or_panic(v[258] == 20);
    assert_or_panic(v[259] == 21);
    assert_or_panic(v[260] == 22);
    assert_or_panic(v[261] == 23);
    assert_or_panic(v[262] == 24);
    assert_or_panic(v[263] == 25);
    assert_or_panic(v[264] == 26);
    assert_or_panic(v[265] == 27);
    assert_or_panic(v[266] == 28);
    assert_or_panic(v[267] == 29);
    assert_or_panic(v[268] == 30);
    assert_or_panic(v[269] == 31);
    assert_or_panic(v[270] == 32);
    assert_or_panic(v[271] == 33);
    assert_or_panic(v[272] == 34);
    assert_or_panic(v[273] == 35);
    assert_or_panic(v[274] == 36);
    assert_or_panic(v[275] == 37);
    assert_or_panic(v[276] == 38);
    assert_or_panic(v[277] == 39);
    assert_or_panic(v[278] == 40);
    assert_or_panic(v[279] == 41);
    assert_or_panic(v[280] == 42);
    assert_or_panic(v[281] == 43);
    assert_or_panic(v[282] == 44);
    assert_or_panic(v[283] == 45);
    assert_or_panic(v[284] == 46);
    assert_or_panic(v[285] == 47);
    assert_or_panic(v[286] == 48);
    assert_or_panic(v[287] == 49);
    assert_or_panic(v[288] == 50);
    assert_or_panic(v[289] == 51);
    assert_or_panic(v[290] == 52);
    assert_or_panic(v[291] == 53);
    assert_or_panic(v[292] == 54);
    assert_or_panic(v[293] == 55);
    assert_or_panic(v[294] == 56);
    assert_or_panic(v[295] == 57);
    assert_or_panic(v[296] == 58);
    assert_or_panic(v[297] == 59);
    assert_or_panic(v[298] == 60);
    assert_or_panic(v[299] == 61);
    assert_or_panic(v[300] == 62);
    assert_or_panic(v[301] == 63);
    assert_or_panic(v[302] == 64);
    assert_or_panic(v[303] == 65);
    assert_or_panic(v[304] == 66);
    assert_or_panic(v[305] == 67);
    assert_or_panic(v[306] == 68);
    assert_or_panic(v[307] == 69);
    assert_or_panic(v[308] == 70);
    assert_or_panic(v[309] == 71);
    assert_or_panic(v[310] == 72);
    assert_or_panic(v[311] == 73);
    assert_or_panic(v[312] == 74);
    assert_or_panic(v[313] == 75);
    assert_or_panic(v[314] == 76);
    assert_or_panic(v[315] == 77);
    assert_or_panic(v[316] == 78);
    assert_or_panic(v[317] == 79);
    assert_or_panic(v[318] == 80);
    assert_or_panic(v[319] == 81);
    assert_or_panic(v[320] == 82);
    assert_or_panic(v[321] == 83);
    assert_or_panic(v[322] == 84);
    assert_or_panic(v[323] == 85);
    assert_or_panic(v[324] == 86);
    assert_or_panic(v[325] == 87);
    assert_or_panic(v[326] == 88);
    assert_or_panic(v[327] == 89);
    assert_or_panic(v[328] == 90);
    assert_or_panic(v[329] == 91);
    assert_or_panic(v[330] == 92);
    assert_or_panic(v[331] == 93);
    assert_or_panic(v[332] == 94);
    assert_or_panic(v[333] == 95);
    assert_or_panic(v[334] == 96);
    assert_or_panic(v[335] == 97);
    assert_or_panic(v[336] == 98);
    assert_or_panic(v[337] == 99);
    assert_or_panic(v[338] == 0);
    assert_or_panic(v[339] == 1);
    assert_or_panic(v[340] == 2);
    assert_or_panic(v[341] == 3);
    assert_or_panic(v[342] == 4);
    assert_or_panic(v[343] == 5);
    assert_or_panic(v[344] == 6);
    assert_or_panic(v[345] == 7);
    assert_or_panic(v[346] == 8);
    assert_or_panic(v[347] == 9);
    assert_or_panic(v[348] == 10);
    assert_or_panic(v[349] == 11);
    assert_or_panic(v[350] == 12);
    assert_or_panic(v[351] == 13);
    assert_or_panic(v[352] == 14);
    assert_or_panic(v[353] == 15);
    assert_or_panic(v[354] == 16);
    assert_or_panic(v[355] == 17);
    assert_or_panic(v[356] == 18);
    assert_or_panic(v[357] == 19);
    assert_or_panic(v[358] == 20);
    assert_or_panic(v[359] == 21);
    assert_or_panic(v[360] == 22);
    assert_or_panic(v[361] == 23);
    assert_or_panic(v[362] == 24);
    assert_or_panic(v[363] == 25);
    assert_or_panic(v[364] == 26);
    assert_or_panic(v[365] == 27);
    assert_or_panic(v[366] == 28);
    assert_or_panic(v[367] == 29);
    assert_or_panic(v[368] == 30);
    assert_or_panic(v[369] == 31);
    assert_or_panic(v[370] == 32);
    assert_or_panic(v[371] == 33);
    assert_or_panic(v[372] == 34);
    assert_or_panic(v[373] == 35);
    assert_or_panic(v[374] == 36);
    assert_or_panic(v[375] == 37);
    assert_or_panic(v[376] == 38);
    assert_or_panic(v[377] == 39);
    assert_or_panic(v[378] == 40);
    assert_or_panic(v[379] == 41);
    assert_or_panic(v[380] == 42);
    assert_or_panic(v[381] == 43);
    assert_or_panic(v[382] == 44);
    assert_or_panic(v[383] == 45);
    assert_or_panic(i == 384);
}
Vector_384_u8 c_ret_vector_384_u8(void);
void c_vector_384_u8(Vector_384_u8, size_t);
void c_test_vector_384_u8(void);
static void test_vector_384_u8(void) {
    c_abi_current_test = "@Vector(384, u8)";
    Vector_384_u8 v = c_ret_vector_384_u8();
    assert_or_panic(v[0] == 46);
    assert_or_panic(v[1] == 47);
    assert_or_panic(v[2] == 48);
    assert_or_panic(v[3] == 49);
    assert_or_panic(v[4] == 50);
    assert_or_panic(v[5] == 51);
    assert_or_panic(v[6] == 52);
    assert_or_panic(v[7] == 53);
    assert_or_panic(v[8] == 54);
    assert_or_panic(v[9] == 55);
    assert_or_panic(v[10] == 56);
    assert_or_panic(v[11] == 57);
    assert_or_panic(v[12] == 58);
    assert_or_panic(v[13] == 59);
    assert_or_panic(v[14] == 60);
    assert_or_panic(v[15] == 61);
    assert_or_panic(v[16] == 62);
    assert_or_panic(v[17] == 63);
    assert_or_panic(v[18] == 64);
    assert_or_panic(v[19] == 65);
    assert_or_panic(v[20] == 66);
    assert_or_panic(v[21] == 67);
    assert_or_panic(v[22] == 68);
    assert_or_panic(v[23] == 69);
    assert_or_panic(v[24] == 70);
    assert_or_panic(v[25] == 71);
    assert_or_panic(v[26] == 72);
    assert_or_panic(v[27] == 73);
    assert_or_panic(v[28] == 74);
    assert_or_panic(v[29] == 75);
    assert_or_panic(v[30] == 76);
    assert_or_panic(v[31] == 77);
    assert_or_panic(v[32] == 78);
    assert_or_panic(v[33] == 79);
    assert_or_panic(v[34] == 80);
    assert_or_panic(v[35] == 81);
    assert_or_panic(v[36] == 82);
    assert_or_panic(v[37] == 83);
    assert_or_panic(v[38] == 84);
    assert_or_panic(v[39] == 85);
    assert_or_panic(v[40] == 86);
    assert_or_panic(v[41] == 87);
    assert_or_panic(v[42] == 88);
    assert_or_panic(v[43] == 89);
    assert_or_panic(v[44] == 90);
    assert_or_panic(v[45] == 91);
    assert_or_panic(v[46] == 92);
    assert_or_panic(v[47] == 93);
    assert_or_panic(v[48] == 94);
    assert_or_panic(v[49] == 95);
    assert_or_panic(v[50] == 96);
    assert_or_panic(v[51] == 97);
    assert_or_panic(v[52] == 98);
    assert_or_panic(v[53] == 99);
    assert_or_panic(v[54] == 0);
    assert_or_panic(v[55] == 1);
    assert_or_panic(v[56] == 2);
    assert_or_panic(v[57] == 3);
    assert_or_panic(v[58] == 4);
    assert_or_panic(v[59] == 5);
    assert_or_panic(v[60] == 6);
    assert_or_panic(v[61] == 7);
    assert_or_panic(v[62] == 8);
    assert_or_panic(v[63] == 9);
    assert_or_panic(v[64] == 10);
    assert_or_panic(v[65] == 11);
    assert_or_panic(v[66] == 12);
    assert_or_panic(v[67] == 13);
    assert_or_panic(v[68] == 14);
    assert_or_panic(v[69] == 15);
    assert_or_panic(v[70] == 16);
    assert_or_panic(v[71] == 17);
    assert_or_panic(v[72] == 18);
    assert_or_panic(v[73] == 19);
    assert_or_panic(v[74] == 20);
    assert_or_panic(v[75] == 21);
    assert_or_panic(v[76] == 22);
    assert_or_panic(v[77] == 23);
    assert_or_panic(v[78] == 24);
    assert_or_panic(v[79] == 25);
    assert_or_panic(v[80] == 26);
    assert_or_panic(v[81] == 27);
    assert_or_panic(v[82] == 28);
    assert_or_panic(v[83] == 29);
    assert_or_panic(v[84] == 30);
    assert_or_panic(v[85] == 31);
    assert_or_panic(v[86] == 32);
    assert_or_panic(v[87] == 33);
    assert_or_panic(v[88] == 34);
    assert_or_panic(v[89] == 35);
    assert_or_panic(v[90] == 36);
    assert_or_panic(v[91] == 37);
    assert_or_panic(v[92] == 38);
    assert_or_panic(v[93] == 39);
    assert_or_panic(v[94] == 40);
    assert_or_panic(v[95] == 41);
    assert_or_panic(v[96] == 42);
    assert_or_panic(v[97] == 43);
    assert_or_panic(v[98] == 44);
    assert_or_panic(v[99] == 45);
    assert_or_panic(v[100] == 46);
    assert_or_panic(v[101] == 47);
    assert_or_panic(v[102] == 48);
    assert_or_panic(v[103] == 49);
    assert_or_panic(v[104] == 50);
    assert_or_panic(v[105] == 51);
    assert_or_panic(v[106] == 52);
    assert_or_panic(v[107] == 53);
    assert_or_panic(v[108] == 54);
    assert_or_panic(v[109] == 55);
    assert_or_panic(v[110] == 56);
    assert_or_panic(v[111] == 57);
    assert_or_panic(v[112] == 58);
    assert_or_panic(v[113] == 59);
    assert_or_panic(v[114] == 60);
    assert_or_panic(v[115] == 61);
    assert_or_panic(v[116] == 62);
    assert_or_panic(v[117] == 63);
    assert_or_panic(v[118] == 64);
    assert_or_panic(v[119] == 65);
    assert_or_panic(v[120] == 66);
    assert_or_panic(v[121] == 67);
    assert_or_panic(v[122] == 68);
    assert_or_panic(v[123] == 69);
    assert_or_panic(v[124] == 70);
    assert_or_panic(v[125] == 71);
    assert_or_panic(v[126] == 72);
    assert_or_panic(v[127] == 73);
    assert_or_panic(v[128] == 74);
    assert_or_panic(v[129] == 75);
    assert_or_panic(v[130] == 76);
    assert_or_panic(v[131] == 77);
    assert_or_panic(v[132] == 78);
    assert_or_panic(v[133] == 79);
    assert_or_panic(v[134] == 80);
    assert_or_panic(v[135] == 81);
    assert_or_panic(v[136] == 82);
    assert_or_panic(v[137] == 83);
    assert_or_panic(v[138] == 84);
    assert_or_panic(v[139] == 85);
    assert_or_panic(v[140] == 86);
    assert_or_panic(v[141] == 87);
    assert_or_panic(v[142] == 88);
    assert_or_panic(v[143] == 89);
    assert_or_panic(v[144] == 90);
    assert_or_panic(v[145] == 91);
    assert_or_panic(v[146] == 92);
    assert_or_panic(v[147] == 93);
    assert_or_panic(v[148] == 94);
    assert_or_panic(v[149] == 95);
    assert_or_panic(v[150] == 96);
    assert_or_panic(v[151] == 97);
    assert_or_panic(v[152] == 98);
    assert_or_panic(v[153] == 99);
    assert_or_panic(v[154] == 0);
    assert_or_panic(v[155] == 1);
    assert_or_panic(v[156] == 2);
    assert_or_panic(v[157] == 3);
    assert_or_panic(v[158] == 4);
    assert_or_panic(v[159] == 5);
    assert_or_panic(v[160] == 6);
    assert_or_panic(v[161] == 7);
    assert_or_panic(v[162] == 8);
    assert_or_panic(v[163] == 9);
    assert_or_panic(v[164] == 10);
    assert_or_panic(v[165] == 11);
    assert_or_panic(v[166] == 12);
    assert_or_panic(v[167] == 13);
    assert_or_panic(v[168] == 14);
    assert_or_panic(v[169] == 15);
    assert_or_panic(v[170] == 16);
    assert_or_panic(v[171] == 17);
    assert_or_panic(v[172] == 18);
    assert_or_panic(v[173] == 19);
    assert_or_panic(v[174] == 20);
    assert_or_panic(v[175] == 21);
    assert_or_panic(v[176] == 22);
    assert_or_panic(v[177] == 23);
    assert_or_panic(v[178] == 24);
    assert_or_panic(v[179] == 25);
    assert_or_panic(v[180] == 26);
    assert_or_panic(v[181] == 27);
    assert_or_panic(v[182] == 28);
    assert_or_panic(v[183] == 29);
    assert_or_panic(v[184] == 30);
    assert_or_panic(v[185] == 31);
    assert_or_panic(v[186] == 32);
    assert_or_panic(v[187] == 33);
    assert_or_panic(v[188] == 34);
    assert_or_panic(v[189] == 35);
    assert_or_panic(v[190] == 36);
    assert_or_panic(v[191] == 37);
    assert_or_panic(v[192] == 38);
    assert_or_panic(v[193] == 39);
    assert_or_panic(v[194] == 40);
    assert_or_panic(v[195] == 41);
    assert_or_panic(v[196] == 42);
    assert_or_panic(v[197] == 43);
    assert_or_panic(v[198] == 44);
    assert_or_panic(v[199] == 45);
    assert_or_panic(v[200] == 46);
    assert_or_panic(v[201] == 47);
    assert_or_panic(v[202] == 48);
    assert_or_panic(v[203] == 49);
    assert_or_panic(v[204] == 50);
    assert_or_panic(v[205] == 51);
    assert_or_panic(v[206] == 52);
    assert_or_panic(v[207] == 53);
    assert_or_panic(v[208] == 54);
    assert_or_panic(v[209] == 55);
    assert_or_panic(v[210] == 56);
    assert_or_panic(v[211] == 57);
    assert_or_panic(v[212] == 58);
    assert_or_panic(v[213] == 59);
    assert_or_panic(v[214] == 60);
    assert_or_panic(v[215] == 61);
    assert_or_panic(v[216] == 62);
    assert_or_panic(v[217] == 63);
    assert_or_panic(v[218] == 64);
    assert_or_panic(v[219] == 65);
    assert_or_panic(v[220] == 66);
    assert_or_panic(v[221] == 67);
    assert_or_panic(v[222] == 68);
    assert_or_panic(v[223] == 69);
    assert_or_panic(v[224] == 70);
    assert_or_panic(v[225] == 71);
    assert_or_panic(v[226] == 72);
    assert_or_panic(v[227] == 73);
    assert_or_panic(v[228] == 74);
    assert_or_panic(v[229] == 75);
    assert_or_panic(v[230] == 76);
    assert_or_panic(v[231] == 77);
    assert_or_panic(v[232] == 78);
    assert_or_panic(v[233] == 79);
    assert_or_panic(v[234] == 80);
    assert_or_panic(v[235] == 81);
    assert_or_panic(v[236] == 82);
    assert_or_panic(v[237] == 83);
    assert_or_panic(v[238] == 84);
    assert_or_panic(v[239] == 85);
    assert_or_panic(v[240] == 86);
    assert_or_panic(v[241] == 87);
    assert_or_panic(v[242] == 88);
    assert_or_panic(v[243] == 89);
    assert_or_panic(v[244] == 90);
    assert_or_panic(v[245] == 91);
    assert_or_panic(v[246] == 92);
    assert_or_panic(v[247] == 93);
    assert_or_panic(v[248] == 94);
    assert_or_panic(v[249] == 95);
    assert_or_panic(v[250] == 96);
    assert_or_panic(v[251] == 97);
    assert_or_panic(v[252] == 98);
    assert_or_panic(v[253] == 99);
    assert_or_panic(v[254] == 0);
    assert_or_panic(v[255] == 1);
    assert_or_panic(v[256] == 2);
    assert_or_panic(v[257] == 3);
    assert_or_panic(v[258] == 4);
    assert_or_panic(v[259] == 5);
    assert_or_panic(v[260] == 6);
    assert_or_panic(v[261] == 7);
    assert_or_panic(v[262] == 8);
    assert_or_panic(v[263] == 9);
    assert_or_panic(v[264] == 10);
    assert_or_panic(v[265] == 11);
    assert_or_panic(v[266] == 12);
    assert_or_panic(v[267] == 13);
    assert_or_panic(v[268] == 14);
    assert_or_panic(v[269] == 15);
    assert_or_panic(v[270] == 16);
    assert_or_panic(v[271] == 17);
    assert_or_panic(v[272] == 18);
    assert_or_panic(v[273] == 19);
    assert_or_panic(v[274] == 20);
    assert_or_panic(v[275] == 21);
    assert_or_panic(v[276] == 22);
    assert_or_panic(v[277] == 23);
    assert_or_panic(v[278] == 24);
    assert_or_panic(v[279] == 25);
    assert_or_panic(v[280] == 26);
    assert_or_panic(v[281] == 27);
    assert_or_panic(v[282] == 28);
    assert_or_panic(v[283] == 29);
    assert_or_panic(v[284] == 30);
    assert_or_panic(v[285] == 31);
    assert_or_panic(v[286] == 32);
    assert_or_panic(v[287] == 33);
    assert_or_panic(v[288] == 34);
    assert_or_panic(v[289] == 35);
    assert_or_panic(v[290] == 36);
    assert_or_panic(v[291] == 37);
    assert_or_panic(v[292] == 38);
    assert_or_panic(v[293] == 39);
    assert_or_panic(v[294] == 40);
    assert_or_panic(v[295] == 41);
    assert_or_panic(v[296] == 42);
    assert_or_panic(v[297] == 43);
    assert_or_panic(v[298] == 44);
    assert_or_panic(v[299] == 45);
    assert_or_panic(v[300] == 46);
    assert_or_panic(v[301] == 47);
    assert_or_panic(v[302] == 48);
    assert_or_panic(v[303] == 49);
    assert_or_panic(v[304] == 50);
    assert_or_panic(v[305] == 51);
    assert_or_panic(v[306] == 52);
    assert_or_panic(v[307] == 53);
    assert_or_panic(v[308] == 54);
    assert_or_panic(v[309] == 55);
    assert_or_panic(v[310] == 56);
    assert_or_panic(v[311] == 57);
    assert_or_panic(v[312] == 58);
    assert_or_panic(v[313] == 59);
    assert_or_panic(v[314] == 60);
    assert_or_panic(v[315] == 61);
    assert_or_panic(v[316] == 62);
    assert_or_panic(v[317] == 63);
    assert_or_panic(v[318] == 64);
    assert_or_panic(v[319] == 65);
    assert_or_panic(v[320] == 66);
    assert_or_panic(v[321] == 67);
    assert_or_panic(v[322] == 68);
    assert_or_panic(v[323] == 69);
    assert_or_panic(v[324] == 70);
    assert_or_panic(v[325] == 71);
    assert_or_panic(v[326] == 72);
    assert_or_panic(v[327] == 73);
    assert_or_panic(v[328] == 74);
    assert_or_panic(v[329] == 75);
    assert_or_panic(v[330] == 76);
    assert_or_panic(v[331] == 77);
    assert_or_panic(v[332] == 78);
    assert_or_panic(v[333] == 79);
    assert_or_panic(v[334] == 80);
    assert_or_panic(v[335] == 81);
    assert_or_panic(v[336] == 82);
    assert_or_panic(v[337] == 83);
    assert_or_panic(v[338] == 84);
    assert_or_panic(v[339] == 85);
    assert_or_panic(v[340] == 86);
    assert_or_panic(v[341] == 87);
    assert_or_panic(v[342] == 88);
    assert_or_panic(v[343] == 89);
    assert_or_panic(v[344] == 90);
    assert_or_panic(v[345] == 91);
    assert_or_panic(v[346] == 92);
    assert_or_panic(v[347] == 93);
    assert_or_panic(v[348] == 94);
    assert_or_panic(v[349] == 95);
    assert_or_panic(v[350] == 96);
    assert_or_panic(v[351] == 97);
    assert_or_panic(v[352] == 98);
    assert_or_panic(v[353] == 99);
    assert_or_panic(v[354] == 0);
    assert_or_panic(v[355] == 1);
    assert_or_panic(v[356] == 2);
    assert_or_panic(v[357] == 3);
    assert_or_panic(v[358] == 4);
    assert_or_panic(v[359] == 5);
    assert_or_panic(v[360] == 6);
    assert_or_panic(v[361] == 7);
    assert_or_panic(v[362] == 8);
    assert_or_panic(v[363] == 9);
    assert_or_panic(v[364] == 10);
    assert_or_panic(v[365] == 11);
    assert_or_panic(v[366] == 12);
    assert_or_panic(v[367] == 13);
    assert_or_panic(v[368] == 14);
    assert_or_panic(v[369] == 15);
    assert_or_panic(v[370] == 16);
    assert_or_panic(v[371] == 17);
    assert_or_panic(v[372] == 18);
    assert_or_panic(v[373] == 19);
    assert_or_panic(v[374] == 20);
    assert_or_panic(v[375] == 21);
    assert_or_panic(v[376] == 22);
    assert_or_panic(v[377] == 23);
    assert_or_panic(v[378] == 24);
    assert_or_panic(v[379] == 25);
    assert_or_panic(v[380] == 26);
    assert_or_panic(v[381] == 27);
    assert_or_panic(v[382] == 28);
    assert_or_panic(v[383] == 29);
    c_vector_384_u8((Vector_384_u8){
        30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
        46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
        62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
        78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
        94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
        42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
        58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
        74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
        90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
        6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
        38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
        70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
        86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
        2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
        50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
        66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
        82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
        98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
    }, 384);
    c_test_vector_384_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint8_t Vector_512_u8 __attribute__((vector_size(512 * sizeof(uint8_t))));
Vector_512_u8 zig_ret_vector_512_u8(void) {
    return (Vector_512_u8){
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
    46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
    78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
    94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
    42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
    74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
    6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
    86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
    2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
    98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
    46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
    78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
    94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    };
}
void zig_vector_512_u8(Vector_512_u8 v, size_t i) {
    assert_or_panic(v[0] == 26);
    assert_or_panic(v[1] == 27);
    assert_or_panic(v[2] == 28);
    assert_or_panic(v[3] == 29);
    assert_or_panic(v[4] == 30);
    assert_or_panic(v[5] == 31);
    assert_or_panic(v[6] == 32);
    assert_or_panic(v[7] == 33);
    assert_or_panic(v[8] == 34);
    assert_or_panic(v[9] == 35);
    assert_or_panic(v[10] == 36);
    assert_or_panic(v[11] == 37);
    assert_or_panic(v[12] == 38);
    assert_or_panic(v[13] == 39);
    assert_or_panic(v[14] == 40);
    assert_or_panic(v[15] == 41);
    assert_or_panic(v[16] == 42);
    assert_or_panic(v[17] == 43);
    assert_or_panic(v[18] == 44);
    assert_or_panic(v[19] == 45);
    assert_or_panic(v[20] == 46);
    assert_or_panic(v[21] == 47);
    assert_or_panic(v[22] == 48);
    assert_or_panic(v[23] == 49);
    assert_or_panic(v[24] == 50);
    assert_or_panic(v[25] == 51);
    assert_or_panic(v[26] == 52);
    assert_or_panic(v[27] == 53);
    assert_or_panic(v[28] == 54);
    assert_or_panic(v[29] == 55);
    assert_or_panic(v[30] == 56);
    assert_or_panic(v[31] == 57);
    assert_or_panic(v[32] == 58);
    assert_or_panic(v[33] == 59);
    assert_or_panic(v[34] == 60);
    assert_or_panic(v[35] == 61);
    assert_or_panic(v[36] == 62);
    assert_or_panic(v[37] == 63);
    assert_or_panic(v[38] == 64);
    assert_or_panic(v[39] == 65);
    assert_or_panic(v[40] == 66);
    assert_or_panic(v[41] == 67);
    assert_or_panic(v[42] == 68);
    assert_or_panic(v[43] == 69);
    assert_or_panic(v[44] == 70);
    assert_or_panic(v[45] == 71);
    assert_or_panic(v[46] == 72);
    assert_or_panic(v[47] == 73);
    assert_or_panic(v[48] == 74);
    assert_or_panic(v[49] == 75);
    assert_or_panic(v[50] == 76);
    assert_or_panic(v[51] == 77);
    assert_or_panic(v[52] == 78);
    assert_or_panic(v[53] == 79);
    assert_or_panic(v[54] == 80);
    assert_or_panic(v[55] == 81);
    assert_or_panic(v[56] == 82);
    assert_or_panic(v[57] == 83);
    assert_or_panic(v[58] == 84);
    assert_or_panic(v[59] == 85);
    assert_or_panic(v[60] == 86);
    assert_or_panic(v[61] == 87);
    assert_or_panic(v[62] == 88);
    assert_or_panic(v[63] == 89);
    assert_or_panic(v[64] == 90);
    assert_or_panic(v[65] == 91);
    assert_or_panic(v[66] == 92);
    assert_or_panic(v[67] == 93);
    assert_or_panic(v[68] == 94);
    assert_or_panic(v[69] == 95);
    assert_or_panic(v[70] == 96);
    assert_or_panic(v[71] == 97);
    assert_or_panic(v[72] == 98);
    assert_or_panic(v[73] == 99);
    assert_or_panic(v[74] == 0);
    assert_or_panic(v[75] == 1);
    assert_or_panic(v[76] == 2);
    assert_or_panic(v[77] == 3);
    assert_or_panic(v[78] == 4);
    assert_or_panic(v[79] == 5);
    assert_or_panic(v[80] == 6);
    assert_or_panic(v[81] == 7);
    assert_or_panic(v[82] == 8);
    assert_or_panic(v[83] == 9);
    assert_or_panic(v[84] == 10);
    assert_or_panic(v[85] == 11);
    assert_or_panic(v[86] == 12);
    assert_or_panic(v[87] == 13);
    assert_or_panic(v[88] == 14);
    assert_or_panic(v[89] == 15);
    assert_or_panic(v[90] == 16);
    assert_or_panic(v[91] == 17);
    assert_or_panic(v[92] == 18);
    assert_or_panic(v[93] == 19);
    assert_or_panic(v[94] == 20);
    assert_or_panic(v[95] == 21);
    assert_or_panic(v[96] == 22);
    assert_or_panic(v[97] == 23);
    assert_or_panic(v[98] == 24);
    assert_or_panic(v[99] == 25);
    assert_or_panic(v[100] == 26);
    assert_or_panic(v[101] == 27);
    assert_or_panic(v[102] == 28);
    assert_or_panic(v[103] == 29);
    assert_or_panic(v[104] == 30);
    assert_or_panic(v[105] == 31);
    assert_or_panic(v[106] == 32);
    assert_or_panic(v[107] == 33);
    assert_or_panic(v[108] == 34);
    assert_or_panic(v[109] == 35);
    assert_or_panic(v[110] == 36);
    assert_or_panic(v[111] == 37);
    assert_or_panic(v[112] == 38);
    assert_or_panic(v[113] == 39);
    assert_or_panic(v[114] == 40);
    assert_or_panic(v[115] == 41);
    assert_or_panic(v[116] == 42);
    assert_or_panic(v[117] == 43);
    assert_or_panic(v[118] == 44);
    assert_or_panic(v[119] == 45);
    assert_or_panic(v[120] == 46);
    assert_or_panic(v[121] == 47);
    assert_or_panic(v[122] == 48);
    assert_or_panic(v[123] == 49);
    assert_or_panic(v[124] == 50);
    assert_or_panic(v[125] == 51);
    assert_or_panic(v[126] == 52);
    assert_or_panic(v[127] == 53);
    assert_or_panic(v[128] == 54);
    assert_or_panic(v[129] == 55);
    assert_or_panic(v[130] == 56);
    assert_or_panic(v[131] == 57);
    assert_or_panic(v[132] == 58);
    assert_or_panic(v[133] == 59);
    assert_or_panic(v[134] == 60);
    assert_or_panic(v[135] == 61);
    assert_or_panic(v[136] == 62);
    assert_or_panic(v[137] == 63);
    assert_or_panic(v[138] == 64);
    assert_or_panic(v[139] == 65);
    assert_or_panic(v[140] == 66);
    assert_or_panic(v[141] == 67);
    assert_or_panic(v[142] == 68);
    assert_or_panic(v[143] == 69);
    assert_or_panic(v[144] == 70);
    assert_or_panic(v[145] == 71);
    assert_or_panic(v[146] == 72);
    assert_or_panic(v[147] == 73);
    assert_or_panic(v[148] == 74);
    assert_or_panic(v[149] == 75);
    assert_or_panic(v[150] == 76);
    assert_or_panic(v[151] == 77);
    assert_or_panic(v[152] == 78);
    assert_or_panic(v[153] == 79);
    assert_or_panic(v[154] == 80);
    assert_or_panic(v[155] == 81);
    assert_or_panic(v[156] == 82);
    assert_or_panic(v[157] == 83);
    assert_or_panic(v[158] == 84);
    assert_or_panic(v[159] == 85);
    assert_or_panic(v[160] == 86);
    assert_or_panic(v[161] == 87);
    assert_or_panic(v[162] == 88);
    assert_or_panic(v[163] == 89);
    assert_or_panic(v[164] == 90);
    assert_or_panic(v[165] == 91);
    assert_or_panic(v[166] == 92);
    assert_or_panic(v[167] == 93);
    assert_or_panic(v[168] == 94);
    assert_or_panic(v[169] == 95);
    assert_or_panic(v[170] == 96);
    assert_or_panic(v[171] == 97);
    assert_or_panic(v[172] == 98);
    assert_or_panic(v[173] == 99);
    assert_or_panic(v[174] == 0);
    assert_or_panic(v[175] == 1);
    assert_or_panic(v[176] == 2);
    assert_or_panic(v[177] == 3);
    assert_or_panic(v[178] == 4);
    assert_or_panic(v[179] == 5);
    assert_or_panic(v[180] == 6);
    assert_or_panic(v[181] == 7);
    assert_or_panic(v[182] == 8);
    assert_or_panic(v[183] == 9);
    assert_or_panic(v[184] == 10);
    assert_or_panic(v[185] == 11);
    assert_or_panic(v[186] == 12);
    assert_or_panic(v[187] == 13);
    assert_or_panic(v[188] == 14);
    assert_or_panic(v[189] == 15);
    assert_or_panic(v[190] == 16);
    assert_or_panic(v[191] == 17);
    assert_or_panic(v[192] == 18);
    assert_or_panic(v[193] == 19);
    assert_or_panic(v[194] == 20);
    assert_or_panic(v[195] == 21);
    assert_or_panic(v[196] == 22);
    assert_or_panic(v[197] == 23);
    assert_or_panic(v[198] == 24);
    assert_or_panic(v[199] == 25);
    assert_or_panic(v[200] == 26);
    assert_or_panic(v[201] == 27);
    assert_or_panic(v[202] == 28);
    assert_or_panic(v[203] == 29);
    assert_or_panic(v[204] == 30);
    assert_or_panic(v[205] == 31);
    assert_or_panic(v[206] == 32);
    assert_or_panic(v[207] == 33);
    assert_or_panic(v[208] == 34);
    assert_or_panic(v[209] == 35);
    assert_or_panic(v[210] == 36);
    assert_or_panic(v[211] == 37);
    assert_or_panic(v[212] == 38);
    assert_or_panic(v[213] == 39);
    assert_or_panic(v[214] == 40);
    assert_or_panic(v[215] == 41);
    assert_or_panic(v[216] == 42);
    assert_or_panic(v[217] == 43);
    assert_or_panic(v[218] == 44);
    assert_or_panic(v[219] == 45);
    assert_or_panic(v[220] == 46);
    assert_or_panic(v[221] == 47);
    assert_or_panic(v[222] == 48);
    assert_or_panic(v[223] == 49);
    assert_or_panic(v[224] == 50);
    assert_or_panic(v[225] == 51);
    assert_or_panic(v[226] == 52);
    assert_or_panic(v[227] == 53);
    assert_or_panic(v[228] == 54);
    assert_or_panic(v[229] == 55);
    assert_or_panic(v[230] == 56);
    assert_or_panic(v[231] == 57);
    assert_or_panic(v[232] == 58);
    assert_or_panic(v[233] == 59);
    assert_or_panic(v[234] == 60);
    assert_or_panic(v[235] == 61);
    assert_or_panic(v[236] == 62);
    assert_or_panic(v[237] == 63);
    assert_or_panic(v[238] == 64);
    assert_or_panic(v[239] == 65);
    assert_or_panic(v[240] == 66);
    assert_or_panic(v[241] == 67);
    assert_or_panic(v[242] == 68);
    assert_or_panic(v[243] == 69);
    assert_or_panic(v[244] == 70);
    assert_or_panic(v[245] == 71);
    assert_or_panic(v[246] == 72);
    assert_or_panic(v[247] == 73);
    assert_or_panic(v[248] == 74);
    assert_or_panic(v[249] == 75);
    assert_or_panic(v[250] == 76);
    assert_or_panic(v[251] == 77);
    assert_or_panic(v[252] == 78);
    assert_or_panic(v[253] == 79);
    assert_or_panic(v[254] == 80);
    assert_or_panic(v[255] == 81);
    assert_or_panic(v[256] == 82);
    assert_or_panic(v[257] == 83);
    assert_or_panic(v[258] == 84);
    assert_or_panic(v[259] == 85);
    assert_or_panic(v[260] == 86);
    assert_or_panic(v[261] == 87);
    assert_or_panic(v[262] == 88);
    assert_or_panic(v[263] == 89);
    assert_or_panic(v[264] == 90);
    assert_or_panic(v[265] == 91);
    assert_or_panic(v[266] == 92);
    assert_or_panic(v[267] == 93);
    assert_or_panic(v[268] == 94);
    assert_or_panic(v[269] == 95);
    assert_or_panic(v[270] == 96);
    assert_or_panic(v[271] == 97);
    assert_or_panic(v[272] == 98);
    assert_or_panic(v[273] == 99);
    assert_or_panic(v[274] == 0);
    assert_or_panic(v[275] == 1);
    assert_or_panic(v[276] == 2);
    assert_or_panic(v[277] == 3);
    assert_or_panic(v[278] == 4);
    assert_or_panic(v[279] == 5);
    assert_or_panic(v[280] == 6);
    assert_or_panic(v[281] == 7);
    assert_or_panic(v[282] == 8);
    assert_or_panic(v[283] == 9);
    assert_or_panic(v[284] == 10);
    assert_or_panic(v[285] == 11);
    assert_or_panic(v[286] == 12);
    assert_or_panic(v[287] == 13);
    assert_or_panic(v[288] == 14);
    assert_or_panic(v[289] == 15);
    assert_or_panic(v[290] == 16);
    assert_or_panic(v[291] == 17);
    assert_or_panic(v[292] == 18);
    assert_or_panic(v[293] == 19);
    assert_or_panic(v[294] == 20);
    assert_or_panic(v[295] == 21);
    assert_or_panic(v[296] == 22);
    assert_or_panic(v[297] == 23);
    assert_or_panic(v[298] == 24);
    assert_or_panic(v[299] == 25);
    assert_or_panic(v[300] == 26);
    assert_or_panic(v[301] == 27);
    assert_or_panic(v[302] == 28);
    assert_or_panic(v[303] == 29);
    assert_or_panic(v[304] == 30);
    assert_or_panic(v[305] == 31);
    assert_or_panic(v[306] == 32);
    assert_or_panic(v[307] == 33);
    assert_or_panic(v[308] == 34);
    assert_or_panic(v[309] == 35);
    assert_or_panic(v[310] == 36);
    assert_or_panic(v[311] == 37);
    assert_or_panic(v[312] == 38);
    assert_or_panic(v[313] == 39);
    assert_or_panic(v[314] == 40);
    assert_or_panic(v[315] == 41);
    assert_or_panic(v[316] == 42);
    assert_or_panic(v[317] == 43);
    assert_or_panic(v[318] == 44);
    assert_or_panic(v[319] == 45);
    assert_or_panic(v[320] == 46);
    assert_or_panic(v[321] == 47);
    assert_or_panic(v[322] == 48);
    assert_or_panic(v[323] == 49);
    assert_or_panic(v[324] == 50);
    assert_or_panic(v[325] == 51);
    assert_or_panic(v[326] == 52);
    assert_or_panic(v[327] == 53);
    assert_or_panic(v[328] == 54);
    assert_or_panic(v[329] == 55);
    assert_or_panic(v[330] == 56);
    assert_or_panic(v[331] == 57);
    assert_or_panic(v[332] == 58);
    assert_or_panic(v[333] == 59);
    assert_or_panic(v[334] == 60);
    assert_or_panic(v[335] == 61);
    assert_or_panic(v[336] == 62);
    assert_or_panic(v[337] == 63);
    assert_or_panic(v[338] == 64);
    assert_or_panic(v[339] == 65);
    assert_or_panic(v[340] == 66);
    assert_or_panic(v[341] == 67);
    assert_or_panic(v[342] == 68);
    assert_or_panic(v[343] == 69);
    assert_or_panic(v[344] == 70);
    assert_or_panic(v[345] == 71);
    assert_or_panic(v[346] == 72);
    assert_or_panic(v[347] == 73);
    assert_or_panic(v[348] == 74);
    assert_or_panic(v[349] == 75);
    assert_or_panic(v[350] == 76);
    assert_or_panic(v[351] == 77);
    assert_or_panic(v[352] == 78);
    assert_or_panic(v[353] == 79);
    assert_or_panic(v[354] == 80);
    assert_or_panic(v[355] == 81);
    assert_or_panic(v[356] == 82);
    assert_or_panic(v[357] == 83);
    assert_or_panic(v[358] == 84);
    assert_or_panic(v[359] == 85);
    assert_or_panic(v[360] == 86);
    assert_or_panic(v[361] == 87);
    assert_or_panic(v[362] == 88);
    assert_or_panic(v[363] == 89);
    assert_or_panic(v[364] == 90);
    assert_or_panic(v[365] == 91);
    assert_or_panic(v[366] == 92);
    assert_or_panic(v[367] == 93);
    assert_or_panic(v[368] == 94);
    assert_or_panic(v[369] == 95);
    assert_or_panic(v[370] == 96);
    assert_or_panic(v[371] == 97);
    assert_or_panic(v[372] == 98);
    assert_or_panic(v[373] == 99);
    assert_or_panic(v[374] == 0);
    assert_or_panic(v[375] == 1);
    assert_or_panic(v[376] == 2);
    assert_or_panic(v[377] == 3);
    assert_or_panic(v[378] == 4);
    assert_or_panic(v[379] == 5);
    assert_or_panic(v[380] == 6);
    assert_or_panic(v[381] == 7);
    assert_or_panic(v[382] == 8);
    assert_or_panic(v[383] == 9);
    assert_or_panic(v[384] == 10);
    assert_or_panic(v[385] == 11);
    assert_or_panic(v[386] == 12);
    assert_or_panic(v[387] == 13);
    assert_or_panic(v[388] == 14);
    assert_or_panic(v[389] == 15);
    assert_or_panic(v[390] == 16);
    assert_or_panic(v[391] == 17);
    assert_or_panic(v[392] == 18);
    assert_or_panic(v[393] == 19);
    assert_or_panic(v[394] == 20);
    assert_or_panic(v[395] == 21);
    assert_or_panic(v[396] == 22);
    assert_or_panic(v[397] == 23);
    assert_or_panic(v[398] == 24);
    assert_or_panic(v[399] == 25);
    assert_or_panic(v[400] == 26);
    assert_or_panic(v[401] == 27);
    assert_or_panic(v[402] == 28);
    assert_or_panic(v[403] == 29);
    assert_or_panic(v[404] == 30);
    assert_or_panic(v[405] == 31);
    assert_or_panic(v[406] == 32);
    assert_or_panic(v[407] == 33);
    assert_or_panic(v[408] == 34);
    assert_or_panic(v[409] == 35);
    assert_or_panic(v[410] == 36);
    assert_or_panic(v[411] == 37);
    assert_or_panic(v[412] == 38);
    assert_or_panic(v[413] == 39);
    assert_or_panic(v[414] == 40);
    assert_or_panic(v[415] == 41);
    assert_or_panic(v[416] == 42);
    assert_or_panic(v[417] == 43);
    assert_or_panic(v[418] == 44);
    assert_or_panic(v[419] == 45);
    assert_or_panic(v[420] == 46);
    assert_or_panic(v[421] == 47);
    assert_or_panic(v[422] == 48);
    assert_or_panic(v[423] == 49);
    assert_or_panic(v[424] == 50);
    assert_or_panic(v[425] == 51);
    assert_or_panic(v[426] == 52);
    assert_or_panic(v[427] == 53);
    assert_or_panic(v[428] == 54);
    assert_or_panic(v[429] == 55);
    assert_or_panic(v[430] == 56);
    assert_or_panic(v[431] == 57);
    assert_or_panic(v[432] == 58);
    assert_or_panic(v[433] == 59);
    assert_or_panic(v[434] == 60);
    assert_or_panic(v[435] == 61);
    assert_or_panic(v[436] == 62);
    assert_or_panic(v[437] == 63);
    assert_or_panic(v[438] == 64);
    assert_or_panic(v[439] == 65);
    assert_or_panic(v[440] == 66);
    assert_or_panic(v[441] == 67);
    assert_or_panic(v[442] == 68);
    assert_or_panic(v[443] == 69);
    assert_or_panic(v[444] == 70);
    assert_or_panic(v[445] == 71);
    assert_or_panic(v[446] == 72);
    assert_or_panic(v[447] == 73);
    assert_or_panic(v[448] == 74);
    assert_or_panic(v[449] == 75);
    assert_or_panic(v[450] == 76);
    assert_or_panic(v[451] == 77);
    assert_or_panic(v[452] == 78);
    assert_or_panic(v[453] == 79);
    assert_or_panic(v[454] == 80);
    assert_or_panic(v[455] == 81);
    assert_or_panic(v[456] == 82);
    assert_or_panic(v[457] == 83);
    assert_or_panic(v[458] == 84);
    assert_or_panic(v[459] == 85);
    assert_or_panic(v[460] == 86);
    assert_or_panic(v[461] == 87);
    assert_or_panic(v[462] == 88);
    assert_or_panic(v[463] == 89);
    assert_or_panic(v[464] == 90);
    assert_or_panic(v[465] == 91);
    assert_or_panic(v[466] == 92);
    assert_or_panic(v[467] == 93);
    assert_or_panic(v[468] == 94);
    assert_or_panic(v[469] == 95);
    assert_or_panic(v[470] == 96);
    assert_or_panic(v[471] == 97);
    assert_or_panic(v[472] == 98);
    assert_or_panic(v[473] == 99);
    assert_or_panic(v[474] == 0);
    assert_or_panic(v[475] == 1);
    assert_or_panic(v[476] == 2);
    assert_or_panic(v[477] == 3);
    assert_or_panic(v[478] == 4);
    assert_or_panic(v[479] == 5);
    assert_or_panic(v[480] == 6);
    assert_or_panic(v[481] == 7);
    assert_or_panic(v[482] == 8);
    assert_or_panic(v[483] == 9);
    assert_or_panic(v[484] == 10);
    assert_or_panic(v[485] == 11);
    assert_or_panic(v[486] == 12);
    assert_or_panic(v[487] == 13);
    assert_or_panic(v[488] == 14);
    assert_or_panic(v[489] == 15);
    assert_or_panic(v[490] == 16);
    assert_or_panic(v[491] == 17);
    assert_or_panic(v[492] == 18);
    assert_or_panic(v[493] == 19);
    assert_or_panic(v[494] == 20);
    assert_or_panic(v[495] == 21);
    assert_or_panic(v[496] == 22);
    assert_or_panic(v[497] == 23);
    assert_or_panic(v[498] == 24);
    assert_or_panic(v[499] == 25);
    assert_or_panic(v[500] == 26);
    assert_or_panic(v[501] == 27);
    assert_or_panic(v[502] == 28);
    assert_or_panic(v[503] == 29);
    assert_or_panic(v[504] == 30);
    assert_or_panic(v[505] == 31);
    assert_or_panic(v[506] == 32);
    assert_or_panic(v[507] == 33);
    assert_or_panic(v[508] == 34);
    assert_or_panic(v[509] == 35);
    assert_or_panic(v[510] == 36);
    assert_or_panic(v[511] == 37);
    assert_or_panic(i == 512);
}
Vector_512_u8 c_ret_vector_512_u8(void);
void c_vector_512_u8(Vector_512_u8, size_t);
void c_test_vector_512_u8(void);
static void test_vector_512_u8(void) {
    c_abi_current_test = "@Vector(512, u8)";
    Vector_512_u8 v = c_ret_vector_512_u8();
    assert_or_panic(v[0] == 38);
    assert_or_panic(v[1] == 39);
    assert_or_panic(v[2] == 40);
    assert_or_panic(v[3] == 41);
    assert_or_panic(v[4] == 42);
    assert_or_panic(v[5] == 43);
    assert_or_panic(v[6] == 44);
    assert_or_panic(v[7] == 45);
    assert_or_panic(v[8] == 46);
    assert_or_panic(v[9] == 47);
    assert_or_panic(v[10] == 48);
    assert_or_panic(v[11] == 49);
    assert_or_panic(v[12] == 50);
    assert_or_panic(v[13] == 51);
    assert_or_panic(v[14] == 52);
    assert_or_panic(v[15] == 53);
    assert_or_panic(v[16] == 54);
    assert_or_panic(v[17] == 55);
    assert_or_panic(v[18] == 56);
    assert_or_panic(v[19] == 57);
    assert_or_panic(v[20] == 58);
    assert_or_panic(v[21] == 59);
    assert_or_panic(v[22] == 60);
    assert_or_panic(v[23] == 61);
    assert_or_panic(v[24] == 62);
    assert_or_panic(v[25] == 63);
    assert_or_panic(v[26] == 64);
    assert_or_panic(v[27] == 65);
    assert_or_panic(v[28] == 66);
    assert_or_panic(v[29] == 67);
    assert_or_panic(v[30] == 68);
    assert_or_panic(v[31] == 69);
    assert_or_panic(v[32] == 70);
    assert_or_panic(v[33] == 71);
    assert_or_panic(v[34] == 72);
    assert_or_panic(v[35] == 73);
    assert_or_panic(v[36] == 74);
    assert_or_panic(v[37] == 75);
    assert_or_panic(v[38] == 76);
    assert_or_panic(v[39] == 77);
    assert_or_panic(v[40] == 78);
    assert_or_panic(v[41] == 79);
    assert_or_panic(v[42] == 80);
    assert_or_panic(v[43] == 81);
    assert_or_panic(v[44] == 82);
    assert_or_panic(v[45] == 83);
    assert_or_panic(v[46] == 84);
    assert_or_panic(v[47] == 85);
    assert_or_panic(v[48] == 86);
    assert_or_panic(v[49] == 87);
    assert_or_panic(v[50] == 88);
    assert_or_panic(v[51] == 89);
    assert_or_panic(v[52] == 90);
    assert_or_panic(v[53] == 91);
    assert_or_panic(v[54] == 92);
    assert_or_panic(v[55] == 93);
    assert_or_panic(v[56] == 94);
    assert_or_panic(v[57] == 95);
    assert_or_panic(v[58] == 96);
    assert_or_panic(v[59] == 97);
    assert_or_panic(v[60] == 98);
    assert_or_panic(v[61] == 99);
    assert_or_panic(v[62] == 0);
    assert_or_panic(v[63] == 1);
    assert_or_panic(v[64] == 2);
    assert_or_panic(v[65] == 3);
    assert_or_panic(v[66] == 4);
    assert_or_panic(v[67] == 5);
    assert_or_panic(v[68] == 6);
    assert_or_panic(v[69] == 7);
    assert_or_panic(v[70] == 8);
    assert_or_panic(v[71] == 9);
    assert_or_panic(v[72] == 10);
    assert_or_panic(v[73] == 11);
    assert_or_panic(v[74] == 12);
    assert_or_panic(v[75] == 13);
    assert_or_panic(v[76] == 14);
    assert_or_panic(v[77] == 15);
    assert_or_panic(v[78] == 16);
    assert_or_panic(v[79] == 17);
    assert_or_panic(v[80] == 18);
    assert_or_panic(v[81] == 19);
    assert_or_panic(v[82] == 20);
    assert_or_panic(v[83] == 21);
    assert_or_panic(v[84] == 22);
    assert_or_panic(v[85] == 23);
    assert_or_panic(v[86] == 24);
    assert_or_panic(v[87] == 25);
    assert_or_panic(v[88] == 26);
    assert_or_panic(v[89] == 27);
    assert_or_panic(v[90] == 28);
    assert_or_panic(v[91] == 29);
    assert_or_panic(v[92] == 30);
    assert_or_panic(v[93] == 31);
    assert_or_panic(v[94] == 32);
    assert_or_panic(v[95] == 33);
    assert_or_panic(v[96] == 34);
    assert_or_panic(v[97] == 35);
    assert_or_panic(v[98] == 36);
    assert_or_panic(v[99] == 37);
    assert_or_panic(v[100] == 38);
    assert_or_panic(v[101] == 39);
    assert_or_panic(v[102] == 40);
    assert_or_panic(v[103] == 41);
    assert_or_panic(v[104] == 42);
    assert_or_panic(v[105] == 43);
    assert_or_panic(v[106] == 44);
    assert_or_panic(v[107] == 45);
    assert_or_panic(v[108] == 46);
    assert_or_panic(v[109] == 47);
    assert_or_panic(v[110] == 48);
    assert_or_panic(v[111] == 49);
    assert_or_panic(v[112] == 50);
    assert_or_panic(v[113] == 51);
    assert_or_panic(v[114] == 52);
    assert_or_panic(v[115] == 53);
    assert_or_panic(v[116] == 54);
    assert_or_panic(v[117] == 55);
    assert_or_panic(v[118] == 56);
    assert_or_panic(v[119] == 57);
    assert_or_panic(v[120] == 58);
    assert_or_panic(v[121] == 59);
    assert_or_panic(v[122] == 60);
    assert_or_panic(v[123] == 61);
    assert_or_panic(v[124] == 62);
    assert_or_panic(v[125] == 63);
    assert_or_panic(v[126] == 64);
    assert_or_panic(v[127] == 65);
    assert_or_panic(v[128] == 66);
    assert_or_panic(v[129] == 67);
    assert_or_panic(v[130] == 68);
    assert_or_panic(v[131] == 69);
    assert_or_panic(v[132] == 70);
    assert_or_panic(v[133] == 71);
    assert_or_panic(v[134] == 72);
    assert_or_panic(v[135] == 73);
    assert_or_panic(v[136] == 74);
    assert_or_panic(v[137] == 75);
    assert_or_panic(v[138] == 76);
    assert_or_panic(v[139] == 77);
    assert_or_panic(v[140] == 78);
    assert_or_panic(v[141] == 79);
    assert_or_panic(v[142] == 80);
    assert_or_panic(v[143] == 81);
    assert_or_panic(v[144] == 82);
    assert_or_panic(v[145] == 83);
    assert_or_panic(v[146] == 84);
    assert_or_panic(v[147] == 85);
    assert_or_panic(v[148] == 86);
    assert_or_panic(v[149] == 87);
    assert_or_panic(v[150] == 88);
    assert_or_panic(v[151] == 89);
    assert_or_panic(v[152] == 90);
    assert_or_panic(v[153] == 91);
    assert_or_panic(v[154] == 92);
    assert_or_panic(v[155] == 93);
    assert_or_panic(v[156] == 94);
    assert_or_panic(v[157] == 95);
    assert_or_panic(v[158] == 96);
    assert_or_panic(v[159] == 97);
    assert_or_panic(v[160] == 98);
    assert_or_panic(v[161] == 99);
    assert_or_panic(v[162] == 0);
    assert_or_panic(v[163] == 1);
    assert_or_panic(v[164] == 2);
    assert_or_panic(v[165] == 3);
    assert_or_panic(v[166] == 4);
    assert_or_panic(v[167] == 5);
    assert_or_panic(v[168] == 6);
    assert_or_panic(v[169] == 7);
    assert_or_panic(v[170] == 8);
    assert_or_panic(v[171] == 9);
    assert_or_panic(v[172] == 10);
    assert_or_panic(v[173] == 11);
    assert_or_panic(v[174] == 12);
    assert_or_panic(v[175] == 13);
    assert_or_panic(v[176] == 14);
    assert_or_panic(v[177] == 15);
    assert_or_panic(v[178] == 16);
    assert_or_panic(v[179] == 17);
    assert_or_panic(v[180] == 18);
    assert_or_panic(v[181] == 19);
    assert_or_panic(v[182] == 20);
    assert_or_panic(v[183] == 21);
    assert_or_panic(v[184] == 22);
    assert_or_panic(v[185] == 23);
    assert_or_panic(v[186] == 24);
    assert_or_panic(v[187] == 25);
    assert_or_panic(v[188] == 26);
    assert_or_panic(v[189] == 27);
    assert_or_panic(v[190] == 28);
    assert_or_panic(v[191] == 29);
    assert_or_panic(v[192] == 30);
    assert_or_panic(v[193] == 31);
    assert_or_panic(v[194] == 32);
    assert_or_panic(v[195] == 33);
    assert_or_panic(v[196] == 34);
    assert_or_panic(v[197] == 35);
    assert_or_panic(v[198] == 36);
    assert_or_panic(v[199] == 37);
    assert_or_panic(v[200] == 38);
    assert_or_panic(v[201] == 39);
    assert_or_panic(v[202] == 40);
    assert_or_panic(v[203] == 41);
    assert_or_panic(v[204] == 42);
    assert_or_panic(v[205] == 43);
    assert_or_panic(v[206] == 44);
    assert_or_panic(v[207] == 45);
    assert_or_panic(v[208] == 46);
    assert_or_panic(v[209] == 47);
    assert_or_panic(v[210] == 48);
    assert_or_panic(v[211] == 49);
    assert_or_panic(v[212] == 50);
    assert_or_panic(v[213] == 51);
    assert_or_panic(v[214] == 52);
    assert_or_panic(v[215] == 53);
    assert_or_panic(v[216] == 54);
    assert_or_panic(v[217] == 55);
    assert_or_panic(v[218] == 56);
    assert_or_panic(v[219] == 57);
    assert_or_panic(v[220] == 58);
    assert_or_panic(v[221] == 59);
    assert_or_panic(v[222] == 60);
    assert_or_panic(v[223] == 61);
    assert_or_panic(v[224] == 62);
    assert_or_panic(v[225] == 63);
    assert_or_panic(v[226] == 64);
    assert_or_panic(v[227] == 65);
    assert_or_panic(v[228] == 66);
    assert_or_panic(v[229] == 67);
    assert_or_panic(v[230] == 68);
    assert_or_panic(v[231] == 69);
    assert_or_panic(v[232] == 70);
    assert_or_panic(v[233] == 71);
    assert_or_panic(v[234] == 72);
    assert_or_panic(v[235] == 73);
    assert_or_panic(v[236] == 74);
    assert_or_panic(v[237] == 75);
    assert_or_panic(v[238] == 76);
    assert_or_panic(v[239] == 77);
    assert_or_panic(v[240] == 78);
    assert_or_panic(v[241] == 79);
    assert_or_panic(v[242] == 80);
    assert_or_panic(v[243] == 81);
    assert_or_panic(v[244] == 82);
    assert_or_panic(v[245] == 83);
    assert_or_panic(v[246] == 84);
    assert_or_panic(v[247] == 85);
    assert_or_panic(v[248] == 86);
    assert_or_panic(v[249] == 87);
    assert_or_panic(v[250] == 88);
    assert_or_panic(v[251] == 89);
    assert_or_panic(v[252] == 90);
    assert_or_panic(v[253] == 91);
    assert_or_panic(v[254] == 92);
    assert_or_panic(v[255] == 93);
    assert_or_panic(v[256] == 94);
    assert_or_panic(v[257] == 95);
    assert_or_panic(v[258] == 96);
    assert_or_panic(v[259] == 97);
    assert_or_panic(v[260] == 98);
    assert_or_panic(v[261] == 99);
    assert_or_panic(v[262] == 0);
    assert_or_panic(v[263] == 1);
    assert_or_panic(v[264] == 2);
    assert_or_panic(v[265] == 3);
    assert_or_panic(v[266] == 4);
    assert_or_panic(v[267] == 5);
    assert_or_panic(v[268] == 6);
    assert_or_panic(v[269] == 7);
    assert_or_panic(v[270] == 8);
    assert_or_panic(v[271] == 9);
    assert_or_panic(v[272] == 10);
    assert_or_panic(v[273] == 11);
    assert_or_panic(v[274] == 12);
    assert_or_panic(v[275] == 13);
    assert_or_panic(v[276] == 14);
    assert_or_panic(v[277] == 15);
    assert_or_panic(v[278] == 16);
    assert_or_panic(v[279] == 17);
    assert_or_panic(v[280] == 18);
    assert_or_panic(v[281] == 19);
    assert_or_panic(v[282] == 20);
    assert_or_panic(v[283] == 21);
    assert_or_panic(v[284] == 22);
    assert_or_panic(v[285] == 23);
    assert_or_panic(v[286] == 24);
    assert_or_panic(v[287] == 25);
    assert_or_panic(v[288] == 26);
    assert_or_panic(v[289] == 27);
    assert_or_panic(v[290] == 28);
    assert_or_panic(v[291] == 29);
    assert_or_panic(v[292] == 30);
    assert_or_panic(v[293] == 31);
    assert_or_panic(v[294] == 32);
    assert_or_panic(v[295] == 33);
    assert_or_panic(v[296] == 34);
    assert_or_panic(v[297] == 35);
    assert_or_panic(v[298] == 36);
    assert_or_panic(v[299] == 37);
    assert_or_panic(v[300] == 38);
    assert_or_panic(v[301] == 39);
    assert_or_panic(v[302] == 40);
    assert_or_panic(v[303] == 41);
    assert_or_panic(v[304] == 42);
    assert_or_panic(v[305] == 43);
    assert_or_panic(v[306] == 44);
    assert_or_panic(v[307] == 45);
    assert_or_panic(v[308] == 46);
    assert_or_panic(v[309] == 47);
    assert_or_panic(v[310] == 48);
    assert_or_panic(v[311] == 49);
    assert_or_panic(v[312] == 50);
    assert_or_panic(v[313] == 51);
    assert_or_panic(v[314] == 52);
    assert_or_panic(v[315] == 53);
    assert_or_panic(v[316] == 54);
    assert_or_panic(v[317] == 55);
    assert_or_panic(v[318] == 56);
    assert_or_panic(v[319] == 57);
    assert_or_panic(v[320] == 58);
    assert_or_panic(v[321] == 59);
    assert_or_panic(v[322] == 60);
    assert_or_panic(v[323] == 61);
    assert_or_panic(v[324] == 62);
    assert_or_panic(v[325] == 63);
    assert_or_panic(v[326] == 64);
    assert_or_panic(v[327] == 65);
    assert_or_panic(v[328] == 66);
    assert_or_panic(v[329] == 67);
    assert_or_panic(v[330] == 68);
    assert_or_panic(v[331] == 69);
    assert_or_panic(v[332] == 70);
    assert_or_panic(v[333] == 71);
    assert_or_panic(v[334] == 72);
    assert_or_panic(v[335] == 73);
    assert_or_panic(v[336] == 74);
    assert_or_panic(v[337] == 75);
    assert_or_panic(v[338] == 76);
    assert_or_panic(v[339] == 77);
    assert_or_panic(v[340] == 78);
    assert_or_panic(v[341] == 79);
    assert_or_panic(v[342] == 80);
    assert_or_panic(v[343] == 81);
    assert_or_panic(v[344] == 82);
    assert_or_panic(v[345] == 83);
    assert_or_panic(v[346] == 84);
    assert_or_panic(v[347] == 85);
    assert_or_panic(v[348] == 86);
    assert_or_panic(v[349] == 87);
    assert_or_panic(v[350] == 88);
    assert_or_panic(v[351] == 89);
    assert_or_panic(v[352] == 90);
    assert_or_panic(v[353] == 91);
    assert_or_panic(v[354] == 92);
    assert_or_panic(v[355] == 93);
    assert_or_panic(v[356] == 94);
    assert_or_panic(v[357] == 95);
    assert_or_panic(v[358] == 96);
    assert_or_panic(v[359] == 97);
    assert_or_panic(v[360] == 98);
    assert_or_panic(v[361] == 99);
    assert_or_panic(v[362] == 0);
    assert_or_panic(v[363] == 1);
    assert_or_panic(v[364] == 2);
    assert_or_panic(v[365] == 3);
    assert_or_panic(v[366] == 4);
    assert_or_panic(v[367] == 5);
    assert_or_panic(v[368] == 6);
    assert_or_panic(v[369] == 7);
    assert_or_panic(v[370] == 8);
    assert_or_panic(v[371] == 9);
    assert_or_panic(v[372] == 10);
    assert_or_panic(v[373] == 11);
    assert_or_panic(v[374] == 12);
    assert_or_panic(v[375] == 13);
    assert_or_panic(v[376] == 14);
    assert_or_panic(v[377] == 15);
    assert_or_panic(v[378] == 16);
    assert_or_panic(v[379] == 17);
    assert_or_panic(v[380] == 18);
    assert_or_panic(v[381] == 19);
    assert_or_panic(v[382] == 20);
    assert_or_panic(v[383] == 21);
    assert_or_panic(v[384] == 22);
    assert_or_panic(v[385] == 23);
    assert_or_panic(v[386] == 24);
    assert_or_panic(v[387] == 25);
    assert_or_panic(v[388] == 26);
    assert_or_panic(v[389] == 27);
    assert_or_panic(v[390] == 28);
    assert_or_panic(v[391] == 29);
    assert_or_panic(v[392] == 30);
    assert_or_panic(v[393] == 31);
    assert_or_panic(v[394] == 32);
    assert_or_panic(v[395] == 33);
    assert_or_panic(v[396] == 34);
    assert_or_panic(v[397] == 35);
    assert_or_panic(v[398] == 36);
    assert_or_panic(v[399] == 37);
    assert_or_panic(v[400] == 38);
    assert_or_panic(v[401] == 39);
    assert_or_panic(v[402] == 40);
    assert_or_panic(v[403] == 41);
    assert_or_panic(v[404] == 42);
    assert_or_panic(v[405] == 43);
    assert_or_panic(v[406] == 44);
    assert_or_panic(v[407] == 45);
    assert_or_panic(v[408] == 46);
    assert_or_panic(v[409] == 47);
    assert_or_panic(v[410] == 48);
    assert_or_panic(v[411] == 49);
    assert_or_panic(v[412] == 50);
    assert_or_panic(v[413] == 51);
    assert_or_panic(v[414] == 52);
    assert_or_panic(v[415] == 53);
    assert_or_panic(v[416] == 54);
    assert_or_panic(v[417] == 55);
    assert_or_panic(v[418] == 56);
    assert_or_panic(v[419] == 57);
    assert_or_panic(v[420] == 58);
    assert_or_panic(v[421] == 59);
    assert_or_panic(v[422] == 60);
    assert_or_panic(v[423] == 61);
    assert_or_panic(v[424] == 62);
    assert_or_panic(v[425] == 63);
    assert_or_panic(v[426] == 64);
    assert_or_panic(v[427] == 65);
    assert_or_panic(v[428] == 66);
    assert_or_panic(v[429] == 67);
    assert_or_panic(v[430] == 68);
    assert_or_panic(v[431] == 69);
    assert_or_panic(v[432] == 70);
    assert_or_panic(v[433] == 71);
    assert_or_panic(v[434] == 72);
    assert_or_panic(v[435] == 73);
    assert_or_panic(v[436] == 74);
    assert_or_panic(v[437] == 75);
    assert_or_panic(v[438] == 76);
    assert_or_panic(v[439] == 77);
    assert_or_panic(v[440] == 78);
    assert_or_panic(v[441] == 79);
    assert_or_panic(v[442] == 80);
    assert_or_panic(v[443] == 81);
    assert_or_panic(v[444] == 82);
    assert_or_panic(v[445] == 83);
    assert_or_panic(v[446] == 84);
    assert_or_panic(v[447] == 85);
    assert_or_panic(v[448] == 86);
    assert_or_panic(v[449] == 87);
    assert_or_panic(v[450] == 88);
    assert_or_panic(v[451] == 89);
    assert_or_panic(v[452] == 90);
    assert_or_panic(v[453] == 91);
    assert_or_panic(v[454] == 92);
    assert_or_panic(v[455] == 93);
    assert_or_panic(v[456] == 94);
    assert_or_panic(v[457] == 95);
    assert_or_panic(v[458] == 96);
    assert_or_panic(v[459] == 97);
    assert_or_panic(v[460] == 98);
    assert_or_panic(v[461] == 99);
    assert_or_panic(v[462] == 0);
    assert_or_panic(v[463] == 1);
    assert_or_panic(v[464] == 2);
    assert_or_panic(v[465] == 3);
    assert_or_panic(v[466] == 4);
    assert_or_panic(v[467] == 5);
    assert_or_panic(v[468] == 6);
    assert_or_panic(v[469] == 7);
    assert_or_panic(v[470] == 8);
    assert_or_panic(v[471] == 9);
    assert_or_panic(v[472] == 10);
    assert_or_panic(v[473] == 11);
    assert_or_panic(v[474] == 12);
    assert_or_panic(v[475] == 13);
    assert_or_panic(v[476] == 14);
    assert_or_panic(v[477] == 15);
    assert_or_panic(v[478] == 16);
    assert_or_panic(v[479] == 17);
    assert_or_panic(v[480] == 18);
    assert_or_panic(v[481] == 19);
    assert_or_panic(v[482] == 20);
    assert_or_panic(v[483] == 21);
    assert_or_panic(v[484] == 22);
    assert_or_panic(v[485] == 23);
    assert_or_panic(v[486] == 24);
    assert_or_panic(v[487] == 25);
    assert_or_panic(v[488] == 26);
    assert_or_panic(v[489] == 27);
    assert_or_panic(v[490] == 28);
    assert_or_panic(v[491] == 29);
    assert_or_panic(v[492] == 30);
    assert_or_panic(v[493] == 31);
    assert_or_panic(v[494] == 32);
    assert_or_panic(v[495] == 33);
    assert_or_panic(v[496] == 34);
    assert_or_panic(v[497] == 35);
    assert_or_panic(v[498] == 36);
    assert_or_panic(v[499] == 37);
    assert_or_panic(v[500] == 38);
    assert_or_panic(v[501] == 39);
    assert_or_panic(v[502] == 40);
    assert_or_panic(v[503] == 41);
    assert_or_panic(v[504] == 42);
    assert_or_panic(v[505] == 43);
    assert_or_panic(v[506] == 44);
    assert_or_panic(v[507] == 45);
    assert_or_panic(v[508] == 46);
    assert_or_panic(v[509] == 47);
    assert_or_panic(v[510] == 48);
    assert_or_panic(v[511] == 49);
    c_vector_512_u8((Vector_512_u8){
        50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
        66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
        82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
        98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
        14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
        30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
        46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
        62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
        78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
        94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
        42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
        58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
        74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
        90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,  2,  3,  4,  5,
        6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
        38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
        70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
        86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 0,  1,
        2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
        50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
        66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
        82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
        98, 99, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
        14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
        30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
        46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    }, 512);
    c_test_vector_512_u8();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_TINY_VECTORS)
typedef uint16_t Vector_1_u16 __attribute__((vector_size(1 * sizeof(uint16_t))));
Vector_1_u16 zig_ret_vector_1_u16(void) {
    return (Vector_1_u16){1};
}
void zig_vector_1_u16(Vector_1_u16 v, size_t i) {
    assert_or_panic(v[0] == 2);
    assert_or_panic(i == 1);
}
Vector_1_u16 c_ret_vector_1_u16(void);
void c_vector_1_u16(Vector_1_u16, size_t);
void c_test_vector_1_u16(void);
static void test_vector_1_u16(void) {
    c_abi_current_test = "@Vector(1, u16)";
#if !(defined(__aarch64__))
    Vector_1_u16 v = c_ret_vector_1_u16();
    assert_or_panic(v[0] == 3);
    c_vector_1_u16((Vector_1_u16){4}, 1);
    c_test_vector_1_u16();
#endif
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint16_t Vector_2_u16 __attribute__((vector_size(2 * sizeof(uint16_t))));
Vector_2_u16 zig_ret_vector_2_u16(void) {
    return (Vector_2_u16){ 5, 6 };
}
void zig_vector_2_u16(Vector_2_u16 v, size_t i) {
    assert_or_panic(v[0] == 7);
    assert_or_panic(v[1] == 8);
    assert_or_panic(i == 2);
}
Vector_2_u16 c_ret_vector_2_u16(void);
void c_vector_2_u16(Vector_2_u16, size_t);
void c_test_vector_2_u16(void);
static void test_vector_2_u16(void) {
    c_abi_current_test = "@Vector(2, u16)";
#if !(defined(__aarch64__))
    Vector_2_u16 v = c_ret_vector_2_u16();
    assert_or_panic(v[0] == 9);
    assert_or_panic(v[1] == 10);
    c_vector_2_u16((Vector_2_u16){ 11, 12 }, 2);
    c_test_vector_2_u16();
#endif
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint16_t Vector_3_u16 __attribute__((vector_size(3 * sizeof(uint16_t))));
Vector_3_u16 zig_ret_vector_3_u16(void) {
    return (Vector_3_u16){ 13, 14, 15 };
}
void zig_vector_3_u16(Vector_3_u16 v, size_t i) {
    assert_or_panic(v[0] == 16);
    assert_or_panic(v[1] == 17);
    assert_or_panic(v[2] == 18);
    assert_or_panic(i == 3);
}
Vector_3_u16 c_ret_vector_3_u16(void);
void c_vector_3_u16(Vector_3_u16, size_t);
void c_test_vector_3_u16(void);
static void test_vector_3_u16(void) {
    c_abi_current_test = "@Vector(3, u16)";
    Vector_3_u16 v = c_ret_vector_3_u16();
    assert_or_panic(v[0] == 19);
    assert_or_panic(v[1] == 20);
    assert_or_panic(v[2] == 21);
    c_vector_3_u16((Vector_3_u16){ 22, 23, 24 }, 3);
    c_test_vector_3_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint16_t Vector_4_u16 __attribute__((vector_size(4 * sizeof(uint16_t))));
Vector_4_u16 zig_ret_vector_4_u16(void) {
    return (Vector_4_u16){ 25, 26, 27, 28 };
}
void zig_vector_4_u16(Vector_4_u16 v, size_t i) {
    assert_or_panic(v[0] == 29);
    assert_or_panic(v[1] == 30);
    assert_or_panic(v[2] == 31);
    assert_or_panic(v[3] == 32);
    assert_or_panic(i == 4);
}
void zig_vector_4_u16_vector_4_u16(Vector_4_u16 v0, Vector_4_u16 v1, size_t i) {
    assert_or_panic(v0[0] == 33);
    assert_or_panic(v0[1] == 34);
    assert_or_panic(v0[2] == 35);
    assert_or_panic(v0[3] == 36);
    assert_or_panic(v1[0] == 37);
    assert_or_panic(v1[1] == 38);
    assert_or_panic(v1[2] == 39);
    assert_or_panic(v1[3] == 40);
    assert_or_panic(i == 8);
}
Vector_4_u16 c_ret_vector_4_u16(void);
void c_vector_4_u16(Vector_4_u16, size_t);
void c_vector_4_u16_vector_4_u16(Vector_4_u16, Vector_4_u16, size_t);
void c_test_vector_4_u16(void);
static void test_vector_4_u16(void) {
    c_abi_current_test = "@Vector(4, u16)";
    Vector_4_u16 v = c_ret_vector_4_u16();
    assert_or_panic(v[0] == 41);
    assert_or_panic(v[1] == 42);
    assert_or_panic(v[2] == 43);
    assert_or_panic(v[3] == 44);
    c_vector_4_u16((Vector_4_u16){ 45, 46, 47, 48 }, 4);
    c_vector_4_u16_vector_4_u16((Vector_4_u16){ 49, 50, 51, 52 }, (Vector_4_u16){ 53, 54, 55, 56 }, 8);
    c_test_vector_4_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint16_t Vector_6_u16 __attribute__((vector_size(6 * sizeof(uint16_t))));
Vector_6_u16 zig_ret_vector_6_u16(void) {
    return (Vector_6_u16){ 41, 42, 43, 44, 45, 46 };
}
void zig_vector_6_u16(Vector_6_u16 v, size_t i) {
    assert_or_panic(v[0] == 47);
    assert_or_panic(v[1] == 48);
    assert_or_panic(v[2] == 49);
    assert_or_panic(v[3] == 50);
    assert_or_panic(v[4] == 51);
    assert_or_panic(v[5] == 52);
    assert_or_panic(i == 6);
}
Vector_6_u16 c_ret_vector_6_u16(void);
void c_vector_6_u16(Vector_6_u16, size_t);
void c_test_vector_6_u16(void);
static void test_vector_6_u16(void) {
    c_abi_current_test = "@Vector(6, u16)";
    Vector_6_u16 v = c_ret_vector_6_u16();
    assert_or_panic(v[0] == 53);
    assert_or_panic(v[1] == 54);
    assert_or_panic(v[2] == 55);
    assert_or_panic(v[3] == 56);
    assert_or_panic(v[4] == 57);
    assert_or_panic(v[5] == 58);
    c_vector_6_u16((Vector_6_u16){ 59, 60, 61, 62, 63, 64 }, 6);
    c_test_vector_6_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint16_t Vector_8_u16 __attribute__((vector_size(8 * sizeof(uint16_t))));
Vector_8_u16 zig_ret_vector_8_u16(void) {
    return (Vector_8_u16){ 65, 66, 67, 68, 69, 70, 71, 72 };
}
void zig_vector_8_u16(Vector_8_u16 v, size_t i) {
    assert_or_panic(v[0] == 73);
    assert_or_panic(v[1] == 74);
    assert_or_panic(v[2] == 75);
    assert_or_panic(v[3] == 76);
    assert_or_panic(v[4] == 77);
    assert_or_panic(v[5] == 78);
    assert_or_panic(v[6] == 79);
    assert_or_panic(v[7] == 80);
    assert_or_panic(i == 8);
}
Vector_8_u16 c_ret_vector_8_u16(void);
void c_vector_8_u16(Vector_8_u16, size_t);
void c_test_vector_8_u16(void);
static void test_vector_8_u16(void) {
    c_abi_current_test = "@Vector(8, u16)";
    Vector_8_u16 v = c_ret_vector_8_u16();
    assert_or_panic(v[0] == 81);
    assert_or_panic(v[1] == 82);
    assert_or_panic(v[2] == 83);
    assert_or_panic(v[3] == 84);
    assert_or_panic(v[4] == 85);
    assert_or_panic(v[5] == 86);
    assert_or_panic(v[6] == 87);
    assert_or_panic(v[7] == 88);
    c_vector_8_u16((Vector_8_u16){ 89, 90, 91, 92, 93, 94, 95, 96 }, 8);
    c_test_vector_8_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint16_t Vector_12_u16 __attribute__((vector_size(12 * sizeof(uint16_t))));
Vector_12_u16 zig_ret_vector_12_u16(void) {
    return (Vector_12_u16){ 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108 };
}
void zig_vector_12_u16(Vector_12_u16 v, size_t i) {
    assert_or_panic(v[0] == 109);
    assert_or_panic(v[1] == 110);
    assert_or_panic(v[2] == 111);
    assert_or_panic(v[3] == 112);
    assert_or_panic(v[4] == 113);
    assert_or_panic(v[5] == 114);
    assert_or_panic(v[6] == 115);
    assert_or_panic(v[7] == 116);
    assert_or_panic(v[8] == 117);
    assert_or_panic(v[9] == 118);
    assert_or_panic(v[10] == 119);
    assert_or_panic(v[11] == 120);
    assert_or_panic(i == 12);
}
Vector_12_u16 c_ret_vector_12_u16(void);
void c_vector_12_u16(Vector_12_u16, size_t);
void c_test_vector_12_u16(void);
static void test_vector_12_u16(void) {
    c_abi_current_test = "@Vector(12, u16)";
    Vector_12_u16 v = c_ret_vector_12_u16();
    assert_or_panic(v[0] == 121);
    assert_or_panic(v[1] == 122);
    assert_or_panic(v[2] == 123);
    assert_or_panic(v[3] == 124);
    assert_or_panic(v[4] == 125);
    assert_or_panic(v[5] == 126);
    assert_or_panic(v[6] == 127);
    assert_or_panic(v[7] == 128);
    assert_or_panic(v[8] == 129);
    assert_or_panic(v[9] == 130);
    assert_or_panic(v[10] == 131);
    assert_or_panic(v[11] == 132);
    c_vector_12_u16((Vector_12_u16){ 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144 }, 12);
    c_test_vector_12_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint16_t Vector_16_u16 __attribute__((vector_size(16 * sizeof(uint16_t))));
Vector_16_u16 zig_ret_vector_16_u16(void) {
    return (Vector_16_u16){ 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160 };
}
void zig_vector_16_u16(Vector_16_u16 v, size_t i) {
    assert_or_panic(v[0] == 161);
    assert_or_panic(v[1] == 162);
    assert_or_panic(v[2] == 163);
    assert_or_panic(v[3] == 164);
    assert_or_panic(v[4] == 165);
    assert_or_panic(v[5] == 166);
    assert_or_panic(v[6] == 167);
    assert_or_panic(v[7] == 168);
    assert_or_panic(v[8] == 169);
    assert_or_panic(v[9] == 170);
    assert_or_panic(v[10] == 171);
    assert_or_panic(v[11] == 172);
    assert_or_panic(v[12] == 173);
    assert_or_panic(v[13] == 174);
    assert_or_panic(v[14] == 175);
    assert_or_panic(v[15] == 176);
    assert_or_panic(i == 16);
}
Vector_16_u16 c_ret_vector_16_u16(void);
void c_vector_16_u16(Vector_16_u16, size_t);
void c_test_vector_16_u16(void);
static void test_vector_16_u16(void) {
    c_abi_current_test = "@Vector(16, u16)";
    Vector_16_u16 v = c_ret_vector_16_u16();
    assert_or_panic(v[0] == 177);
    assert_or_panic(v[1] == 178);
    assert_or_panic(v[2] == 179);
    assert_or_panic(v[3] == 180);
    assert_or_panic(v[4] == 181);
    assert_or_panic(v[5] == 182);
    assert_or_panic(v[6] == 183);
    assert_or_panic(v[7] == 184);
    assert_or_panic(v[8] == 185);
    assert_or_panic(v[9] == 186);
    assert_or_panic(v[10] == 187);
    assert_or_panic(v[11] == 188);
    assert_or_panic(v[12] == 189);
    assert_or_panic(v[13] == 190);
    assert_or_panic(v[14] == 191);
    assert_or_panic(v[15] == 192);
    c_vector_16_u16((Vector_16_u16){ 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208 }, 16);
    c_test_vector_16_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint16_t Vector_24_u16 __attribute__((vector_size(24 * sizeof(uint16_t))));
Vector_24_u16 zig_ret_vector_24_u16(void) {
    return (Vector_24_u16){
    209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232,
    };
}
void zig_vector_24_u16(Vector_24_u16 v, size_t i) {
    assert_or_panic(v[0] == 233);
    assert_or_panic(v[1] == 234);
    assert_or_panic(v[2] == 235);
    assert_or_panic(v[3] == 236);
    assert_or_panic(v[4] == 237);
    assert_or_panic(v[5] == 238);
    assert_or_panic(v[6] == 239);
    assert_or_panic(v[7] == 240);
    assert_or_panic(v[8] == 241);
    assert_or_panic(v[9] == 242);
    assert_or_panic(v[10] == 243);
    assert_or_panic(v[11] == 244);
    assert_or_panic(v[12] == 245);
    assert_or_panic(v[13] == 246);
    assert_or_panic(v[14] == 247);
    assert_or_panic(v[15] == 248);
    assert_or_panic(v[16] == 249);
    assert_or_panic(v[17] == 250);
    assert_or_panic(v[18] == 251);
    assert_or_panic(v[19] == 252);
    assert_or_panic(v[20] == 253);
    assert_or_panic(v[21] == 254);
    assert_or_panic(v[22] == 255);
    assert_or_panic(v[23] == 256);
    assert_or_panic(i == 24);
}
Vector_24_u16 c_ret_vector_24_u16(void);
void c_vector_24_u16(Vector_24_u16, size_t);
void c_test_vector_24_u16(void);
static void test_vector_24_u16(void) {
    c_abi_current_test = "@Vector(24, u16)";
    Vector_24_u16 v = c_ret_vector_24_u16();
    assert_or_panic(v[0] == 257);
    assert_or_panic(v[1] == 258);
    assert_or_panic(v[2] == 259);
    assert_or_panic(v[3] == 260);
    assert_or_panic(v[4] == 261);
    assert_or_panic(v[5] == 262);
    assert_or_panic(v[6] == 263);
    assert_or_panic(v[7] == 264);
    assert_or_panic(v[8] == 265);
    assert_or_panic(v[9] == 266);
    assert_or_panic(v[10] == 267);
    assert_or_panic(v[11] == 268);
    assert_or_panic(v[12] == 269);
    assert_or_panic(v[13] == 270);
    assert_or_panic(v[14] == 271);
    assert_or_panic(v[15] == 272);
    assert_or_panic(v[16] == 273);
    assert_or_panic(v[17] == 274);
    assert_or_panic(v[18] == 275);
    assert_or_panic(v[19] == 276);
    assert_or_panic(v[20] == 277);
    assert_or_panic(v[21] == 278);
    assert_or_panic(v[22] == 279);
    assert_or_panic(v[23] == 280);
    c_vector_24_u16((Vector_24_u16){
        281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296,
        297, 298, 299, 300, 301, 302, 303, 304,
    }, 24);
    c_test_vector_24_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint16_t Vector_32_u16 __attribute__((vector_size(32 * sizeof(uint16_t))));
Vector_32_u16 zig_ret_vector_32_u16(void) {
    return (Vector_32_u16){
    305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320,
    321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336,
    };
}
void zig_vector_32_u16(Vector_32_u16 v, size_t i) {
    assert_or_panic(v[0] == 337);
    assert_or_panic(v[1] == 338);
    assert_or_panic(v[2] == 339);
    assert_or_panic(v[3] == 340);
    assert_or_panic(v[4] == 341);
    assert_or_panic(v[5] == 342);
    assert_or_panic(v[6] == 343);
    assert_or_panic(v[7] == 344);
    assert_or_panic(v[8] == 345);
    assert_or_panic(v[9] == 346);
    assert_or_panic(v[10] == 347);
    assert_or_panic(v[11] == 348);
    assert_or_panic(v[12] == 349);
    assert_or_panic(v[13] == 350);
    assert_or_panic(v[14] == 351);
    assert_or_panic(v[15] == 352);
    assert_or_panic(v[16] == 353);
    assert_or_panic(v[17] == 354);
    assert_or_panic(v[18] == 355);
    assert_or_panic(v[19] == 356);
    assert_or_panic(v[20] == 357);
    assert_or_panic(v[21] == 358);
    assert_or_panic(v[22] == 359);
    assert_or_panic(v[23] == 360);
    assert_or_panic(v[24] == 361);
    assert_or_panic(v[25] == 362);
    assert_or_panic(v[26] == 363);
    assert_or_panic(v[27] == 364);
    assert_or_panic(v[28] == 365);
    assert_or_panic(v[29] == 366);
    assert_or_panic(v[30] == 367);
    assert_or_panic(v[31] == 368);
    assert_or_panic(i == 32);
}
Vector_32_u16 c_ret_vector_32_u16(void);
void c_vector_32_u16(Vector_32_u16, size_t);
void c_test_vector_32_u16(void);
static void test_vector_32_u16(void) {
    c_abi_current_test = "@Vector(32, u16)";
    Vector_32_u16 v = c_ret_vector_32_u16();
    assert_or_panic(v[0] == 369);
    assert_or_panic(v[1] == 370);
    assert_or_panic(v[2] == 371);
    assert_or_panic(v[3] == 372);
    assert_or_panic(v[4] == 373);
    assert_or_panic(v[5] == 374);
    assert_or_panic(v[6] == 375);
    assert_or_panic(v[7] == 376);
    assert_or_panic(v[8] == 377);
    assert_or_panic(v[9] == 378);
    assert_or_panic(v[10] == 379);
    assert_or_panic(v[11] == 380);
    assert_or_panic(v[12] == 381);
    assert_or_panic(v[13] == 382);
    assert_or_panic(v[14] == 383);
    assert_or_panic(v[15] == 384);
    assert_or_panic(v[16] == 385);
    assert_or_panic(v[17] == 386);
    assert_or_panic(v[18] == 387);
    assert_or_panic(v[19] == 388);
    assert_or_panic(v[20] == 389);
    assert_or_panic(v[21] == 390);
    assert_or_panic(v[22] == 391);
    assert_or_panic(v[23] == 392);
    assert_or_panic(v[24] == 393);
    assert_or_panic(v[25] == 394);
    assert_or_panic(v[26] == 395);
    assert_or_panic(v[27] == 396);
    assert_or_panic(v[28] == 397);
    assert_or_panic(v[29] == 398);
    assert_or_panic(v[30] == 399);
    assert_or_panic(v[31] == 400);
    c_vector_32_u16((Vector_32_u16){
        401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416,
        417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432,
    }, 32);
    c_test_vector_32_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint16_t Vector_48_u16 __attribute__((vector_size(48 * sizeof(uint16_t))));
Vector_48_u16 zig_ret_vector_48_u16(void) {
    return (Vector_48_u16){
    433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448,
    449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464,
    465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    };
}
void zig_vector_48_u16(Vector_48_u16 v, size_t i) {
    assert_or_panic(v[0] == 481);
    assert_or_panic(v[1] == 482);
    assert_or_panic(v[2] == 483);
    assert_or_panic(v[3] == 484);
    assert_or_panic(v[4] == 485);
    assert_or_panic(v[5] == 486);
    assert_or_panic(v[6] == 487);
    assert_or_panic(v[7] == 488);
    assert_or_panic(v[8] == 489);
    assert_or_panic(v[9] == 490);
    assert_or_panic(v[10] == 491);
    assert_or_panic(v[11] == 492);
    assert_or_panic(v[12] == 493);
    assert_or_panic(v[13] == 494);
    assert_or_panic(v[14] == 495);
    assert_or_panic(v[15] == 496);
    assert_or_panic(v[16] == 497);
    assert_or_panic(v[17] == 498);
    assert_or_panic(v[18] == 499);
    assert_or_panic(v[19] == 500);
    assert_or_panic(v[20] == 501);
    assert_or_panic(v[21] == 502);
    assert_or_panic(v[22] == 503);
    assert_or_panic(v[23] == 504);
    assert_or_panic(v[24] == 505);
    assert_or_panic(v[25] == 506);
    assert_or_panic(v[26] == 507);
    assert_or_panic(v[27] == 508);
    assert_or_panic(v[28] == 509);
    assert_or_panic(v[29] == 510);
    assert_or_panic(v[30] == 511);
    assert_or_panic(v[31] == 512);
    assert_or_panic(v[32] == 513);
    assert_or_panic(v[33] == 514);
    assert_or_panic(v[34] == 515);
    assert_or_panic(v[35] == 516);
    assert_or_panic(v[36] == 517);
    assert_or_panic(v[37] == 518);
    assert_or_panic(v[38] == 519);
    assert_or_panic(v[39] == 520);
    assert_or_panic(v[40] == 521);
    assert_or_panic(v[41] == 522);
    assert_or_panic(v[42] == 523);
    assert_or_panic(v[43] == 524);
    assert_or_panic(v[44] == 525);
    assert_or_panic(v[45] == 526);
    assert_or_panic(v[46] == 527);
    assert_or_panic(v[47] == 528);
    assert_or_panic(i == 48);
}
Vector_48_u16 c_ret_vector_48_u16(void);
void c_vector_48_u16(Vector_48_u16, size_t);
void c_test_vector_48_u16(void);
static void test_vector_48_u16(void) {
    c_abi_current_test = "@Vector(48, u16)";
    Vector_48_u16 v = c_ret_vector_48_u16();
    assert_or_panic(v[0] == 529);
    assert_or_panic(v[1] == 530);
    assert_or_panic(v[2] == 531);
    assert_or_panic(v[3] == 532);
    assert_or_panic(v[4] == 533);
    assert_or_panic(v[5] == 534);
    assert_or_panic(v[6] == 535);
    assert_or_panic(v[7] == 536);
    assert_or_panic(v[8] == 537);
    assert_or_panic(v[9] == 538);
    assert_or_panic(v[10] == 539);
    assert_or_panic(v[11] == 540);
    assert_or_panic(v[12] == 541);
    assert_or_panic(v[13] == 542);
    assert_or_panic(v[14] == 543);
    assert_or_panic(v[15] == 544);
    assert_or_panic(v[16] == 545);
    assert_or_panic(v[17] == 546);
    assert_or_panic(v[18] == 547);
    assert_or_panic(v[19] == 548);
    assert_or_panic(v[20] == 549);
    assert_or_panic(v[21] == 550);
    assert_or_panic(v[22] == 551);
    assert_or_panic(v[23] == 552);
    assert_or_panic(v[24] == 553);
    assert_or_panic(v[25] == 554);
    assert_or_panic(v[26] == 555);
    assert_or_panic(v[27] == 556);
    assert_or_panic(v[28] == 557);
    assert_or_panic(v[29] == 558);
    assert_or_panic(v[30] == 559);
    assert_or_panic(v[31] == 560);
    assert_or_panic(v[32] == 561);
    assert_or_panic(v[33] == 562);
    assert_or_panic(v[34] == 563);
    assert_or_panic(v[35] == 564);
    assert_or_panic(v[36] == 565);
    assert_or_panic(v[37] == 566);
    assert_or_panic(v[38] == 567);
    assert_or_panic(v[39] == 568);
    assert_or_panic(v[40] == 569);
    assert_or_panic(v[41] == 570);
    assert_or_panic(v[42] == 571);
    assert_or_panic(v[43] == 572);
    assert_or_panic(v[44] == 573);
    assert_or_panic(v[45] == 574);
    assert_or_panic(v[46] == 575);
    assert_or_panic(v[47] == 576);
    c_vector_48_u16((Vector_48_u16){
        577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592,
        593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608,
        609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624,
    }, 48);
    c_test_vector_48_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint16_t Vector_64_u16 __attribute__((vector_size(64 * sizeof(uint16_t))));
Vector_64_u16 zig_ret_vector_64_u16(void) {
    return (Vector_64_u16){
    625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640,
    641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656,
    657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672,
    673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688,
    };
}
void zig_vector_64_u16(Vector_64_u16 v, size_t i) {
    assert_or_panic(v[0] == 689);
    assert_or_panic(v[1] == 690);
    assert_or_panic(v[2] == 691);
    assert_or_panic(v[3] == 692);
    assert_or_panic(v[4] == 693);
    assert_or_panic(v[5] == 694);
    assert_or_panic(v[6] == 695);
    assert_or_panic(v[7] == 696);
    assert_or_panic(v[8] == 697);
    assert_or_panic(v[9] == 698);
    assert_or_panic(v[10] == 699);
    assert_or_panic(v[11] == 700);
    assert_or_panic(v[12] == 701);
    assert_or_panic(v[13] == 702);
    assert_or_panic(v[14] == 703);
    assert_or_panic(v[15] == 704);
    assert_or_panic(v[16] == 705);
    assert_or_panic(v[17] == 706);
    assert_or_panic(v[18] == 707);
    assert_or_panic(v[19] == 708);
    assert_or_panic(v[20] == 709);
    assert_or_panic(v[21] == 710);
    assert_or_panic(v[22] == 711);
    assert_or_panic(v[23] == 712);
    assert_or_panic(v[24] == 713);
    assert_or_panic(v[25] == 714);
    assert_or_panic(v[26] == 715);
    assert_or_panic(v[27] == 716);
    assert_or_panic(v[28] == 717);
    assert_or_panic(v[29] == 718);
    assert_or_panic(v[30] == 719);
    assert_or_panic(v[31] == 720);
    assert_or_panic(v[32] == 721);
    assert_or_panic(v[33] == 722);
    assert_or_panic(v[34] == 723);
    assert_or_panic(v[35] == 724);
    assert_or_panic(v[36] == 725);
    assert_or_panic(v[37] == 726);
    assert_or_panic(v[38] == 727);
    assert_or_panic(v[39] == 728);
    assert_or_panic(v[40] == 729);
    assert_or_panic(v[41] == 730);
    assert_or_panic(v[42] == 731);
    assert_or_panic(v[43] == 732);
    assert_or_panic(v[44] == 733);
    assert_or_panic(v[45] == 734);
    assert_or_panic(v[46] == 735);
    assert_or_panic(v[47] == 736);
    assert_or_panic(v[48] == 737);
    assert_or_panic(v[49] == 738);
    assert_or_panic(v[50] == 739);
    assert_or_panic(v[51] == 740);
    assert_or_panic(v[52] == 741);
    assert_or_panic(v[53] == 742);
    assert_or_panic(v[54] == 743);
    assert_or_panic(v[55] == 744);
    assert_or_panic(v[56] == 745);
    assert_or_panic(v[57] == 746);
    assert_or_panic(v[58] == 747);
    assert_or_panic(v[59] == 748);
    assert_or_panic(v[60] == 749);
    assert_or_panic(v[61] == 750);
    assert_or_panic(v[62] == 751);
    assert_or_panic(v[63] == 752);
    assert_or_panic(i == 64);
}
Vector_64_u16 c_ret_vector_64_u16(void);
void c_vector_64_u16(Vector_64_u16, size_t);
void c_test_vector_64_u16(void);
static void test_vector_64_u16(void) {
    c_abi_current_test = "@Vector(64, u16)";
    Vector_64_u16 v = c_ret_vector_64_u16();
    assert_or_panic(v[0] == 753);
    assert_or_panic(v[1] == 754);
    assert_or_panic(v[2] == 755);
    assert_or_panic(v[3] == 756);
    assert_or_panic(v[4] == 757);
    assert_or_panic(v[5] == 758);
    assert_or_panic(v[6] == 759);
    assert_or_panic(v[7] == 760);
    assert_or_panic(v[8] == 761);
    assert_or_panic(v[9] == 762);
    assert_or_panic(v[10] == 763);
    assert_or_panic(v[11] == 764);
    assert_or_panic(v[12] == 765);
    assert_or_panic(v[13] == 766);
    assert_or_panic(v[14] == 767);
    assert_or_panic(v[15] == 768);
    assert_or_panic(v[16] == 769);
    assert_or_panic(v[17] == 770);
    assert_or_panic(v[18] == 771);
    assert_or_panic(v[19] == 772);
    assert_or_panic(v[20] == 773);
    assert_or_panic(v[21] == 774);
    assert_or_panic(v[22] == 775);
    assert_or_panic(v[23] == 776);
    assert_or_panic(v[24] == 777);
    assert_or_panic(v[25] == 778);
    assert_or_panic(v[26] == 779);
    assert_or_panic(v[27] == 780);
    assert_or_panic(v[28] == 781);
    assert_or_panic(v[29] == 782);
    assert_or_panic(v[30] == 783);
    assert_or_panic(v[31] == 784);
    assert_or_panic(v[32] == 785);
    assert_or_panic(v[33] == 786);
    assert_or_panic(v[34] == 787);
    assert_or_panic(v[35] == 788);
    assert_or_panic(v[36] == 789);
    assert_or_panic(v[37] == 790);
    assert_or_panic(v[38] == 791);
    assert_or_panic(v[39] == 792);
    assert_or_panic(v[40] == 793);
    assert_or_panic(v[41] == 794);
    assert_or_panic(v[42] == 795);
    assert_or_panic(v[43] == 796);
    assert_or_panic(v[44] == 797);
    assert_or_panic(v[45] == 798);
    assert_or_panic(v[46] == 799);
    assert_or_panic(v[47] == 800);
    assert_or_panic(v[48] == 801);
    assert_or_panic(v[49] == 802);
    assert_or_panic(v[50] == 803);
    assert_or_panic(v[51] == 804);
    assert_or_panic(v[52] == 805);
    assert_or_panic(v[53] == 806);
    assert_or_panic(v[54] == 807);
    assert_or_panic(v[55] == 808);
    assert_or_panic(v[56] == 809);
    assert_or_panic(v[57] == 810);
    assert_or_panic(v[58] == 811);
    assert_or_panic(v[59] == 812);
    assert_or_panic(v[60] == 813);
    assert_or_panic(v[61] == 814);
    assert_or_panic(v[62] == 815);
    assert_or_panic(v[63] == 816);
    c_vector_64_u16((Vector_64_u16){
        817, 818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 832,
        833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848,
        849, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864,
        865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880,
    }, 64);
    c_test_vector_64_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint16_t Vector_96_u16 __attribute__((vector_size(96 * sizeof(uint16_t))));
Vector_96_u16 zig_ret_vector_96_u16(void) {
    return (Vector_96_u16){
    890, 891, 892, 893, 894, 895, 896, 897, 898, 899, 900, 901, 902, 903, 904, 905,
    906, 907, 908, 909, 910, 911, 912, 913, 914, 915, 916, 917, 918, 919, 920, 921,
    922, 923, 924, 925, 926, 927, 928, 929, 930, 931, 932, 933, 934, 935, 936, 937,
    938, 939, 940, 941, 942, 943, 944, 945, 946, 947, 948, 949, 950, 951, 952, 953,
    954, 955, 956, 957, 958, 959, 960, 961, 962, 963, 964, 965, 966, 967, 968, 969,
    970, 971, 972, 973, 974, 975, 976, 977, 978, 979, 980, 981, 982, 983, 984, 985,
    };
}
void zig_vector_96_u16(Vector_96_u16 v, size_t i) {
    assert_or_panic(v[0] == 986);
    assert_or_panic(v[1] == 987);
    assert_or_panic(v[2] == 988);
    assert_or_panic(v[3] == 989);
    assert_or_panic(v[4] == 990);
    assert_or_panic(v[5] == 991);
    assert_or_panic(v[6] == 992);
    assert_or_panic(v[7] == 993);
    assert_or_panic(v[8] == 994);
    assert_or_panic(v[9] == 995);
    assert_or_panic(v[10] == 996);
    assert_or_panic(v[11] == 997);
    assert_or_panic(v[12] == 998);
    assert_or_panic(v[13] == 999);
    assert_or_panic(v[14] == 1000);
    assert_or_panic(v[15] == 1001);
    assert_or_panic(v[16] == 1002);
    assert_or_panic(v[17] == 1003);
    assert_or_panic(v[18] == 1004);
    assert_or_panic(v[19] == 1005);
    assert_or_panic(v[20] == 1006);
    assert_or_panic(v[21] == 1007);
    assert_or_panic(v[22] == 1008);
    assert_or_panic(v[23] == 1009);
    assert_or_panic(v[24] == 1010);
    assert_or_panic(v[25] == 1011);
    assert_or_panic(v[26] == 1012);
    assert_or_panic(v[27] == 1013);
    assert_or_panic(v[28] == 1014);
    assert_or_panic(v[29] == 1015);
    assert_or_panic(v[30] == 1016);
    assert_or_panic(v[31] == 1017);
    assert_or_panic(v[32] == 1018);
    assert_or_panic(v[33] == 1019);
    assert_or_panic(v[34] == 1020);
    assert_or_panic(v[35] == 1021);
    assert_or_panic(v[36] == 1022);
    assert_or_panic(v[37] == 1023);
    assert_or_panic(v[38] == 1024);
    assert_or_panic(v[39] == 1025);
    assert_or_panic(v[40] == 1026);
    assert_or_panic(v[41] == 1027);
    assert_or_panic(v[42] == 1028);
    assert_or_panic(v[43] == 1029);
    assert_or_panic(v[44] == 1030);
    assert_or_panic(v[45] == 1031);
    assert_or_panic(v[46] == 1032);
    assert_or_panic(v[47] == 1033);
    assert_or_panic(v[48] == 1034);
    assert_or_panic(v[49] == 1035);
    assert_or_panic(v[50] == 1036);
    assert_or_panic(v[51] == 1037);
    assert_or_panic(v[52] == 1038);
    assert_or_panic(v[53] == 1039);
    assert_or_panic(v[54] == 1040);
    assert_or_panic(v[55] == 1041);
    assert_or_panic(v[56] == 1042);
    assert_or_panic(v[57] == 1043);
    assert_or_panic(v[58] == 1044);
    assert_or_panic(v[59] == 1045);
    assert_or_panic(v[60] == 1046);
    assert_or_panic(v[61] == 1047);
    assert_or_panic(v[62] == 1048);
    assert_or_panic(v[63] == 1049);
    assert_or_panic(v[64] == 1050);
    assert_or_panic(v[65] == 1051);
    assert_or_panic(v[66] == 1052);
    assert_or_panic(v[67] == 1053);
    assert_or_panic(v[68] == 1054);
    assert_or_panic(v[69] == 1055);
    assert_or_panic(v[70] == 1056);
    assert_or_panic(v[71] == 1057);
    assert_or_panic(v[72] == 1058);
    assert_or_panic(v[73] == 1059);
    assert_or_panic(v[74] == 1060);
    assert_or_panic(v[75] == 1061);
    assert_or_panic(v[76] == 1062);
    assert_or_panic(v[77] == 1063);
    assert_or_panic(v[78] == 1064);
    assert_or_panic(v[79] == 1065);
    assert_or_panic(v[80] == 1066);
    assert_or_panic(v[81] == 1067);
    assert_or_panic(v[82] == 1068);
    assert_or_panic(v[83] == 1069);
    assert_or_panic(v[84] == 1070);
    assert_or_panic(v[85] == 1071);
    assert_or_panic(v[86] == 1072);
    assert_or_panic(v[87] == 1073);
    assert_or_panic(v[88] == 1074);
    assert_or_panic(v[89] == 1075);
    assert_or_panic(v[90] == 1076);
    assert_or_panic(v[91] == 1077);
    assert_or_panic(v[92] == 1078);
    assert_or_panic(v[93] == 1079);
    assert_or_panic(v[94] == 1080);
    assert_or_panic(v[95] == 1081);
    assert_or_panic(i == 96);
}
Vector_96_u16 c_ret_vector_96_u16(void);
void c_vector_96_u16(Vector_96_u16, size_t);
void c_test_vector_96_u16(void);
static void test_vector_96_u16(void) {
    c_abi_current_test = "@Vector(96, u16)";
    Vector_96_u16 v = c_ret_vector_96_u16();
    assert_or_panic(v[0] == 1082);
    assert_or_panic(v[1] == 1083);
    assert_or_panic(v[2] == 1084);
    assert_or_panic(v[3] == 1085);
    assert_or_panic(v[4] == 1086);
    assert_or_panic(v[5] == 1087);
    assert_or_panic(v[6] == 1088);
    assert_or_panic(v[7] == 1089);
    assert_or_panic(v[8] == 1090);
    assert_or_panic(v[9] == 1091);
    assert_or_panic(v[10] == 1092);
    assert_or_panic(v[11] == 1093);
    assert_or_panic(v[12] == 1094);
    assert_or_panic(v[13] == 1095);
    assert_or_panic(v[14] == 1096);
    assert_or_panic(v[15] == 1097);
    assert_or_panic(v[16] == 1098);
    assert_or_panic(v[17] == 1099);
    assert_or_panic(v[18] == 1100);
    assert_or_panic(v[19] == 1101);
    assert_or_panic(v[20] == 1102);
    assert_or_panic(v[21] == 1103);
    assert_or_panic(v[22] == 1104);
    assert_or_panic(v[23] == 1105);
    assert_or_panic(v[24] == 1106);
    assert_or_panic(v[25] == 1107);
    assert_or_panic(v[26] == 1108);
    assert_or_panic(v[27] == 1109);
    assert_or_panic(v[28] == 1110);
    assert_or_panic(v[29] == 1111);
    assert_or_panic(v[30] == 1112);
    assert_or_panic(v[31] == 1113);
    assert_or_panic(v[32] == 1114);
    assert_or_panic(v[33] == 1115);
    assert_or_panic(v[34] == 1116);
    assert_or_panic(v[35] == 1117);
    assert_or_panic(v[36] == 1118);
    assert_or_panic(v[37] == 1119);
    assert_or_panic(v[38] == 1120);
    assert_or_panic(v[39] == 1121);
    assert_or_panic(v[40] == 1122);
    assert_or_panic(v[41] == 1123);
    assert_or_panic(v[42] == 1124);
    assert_or_panic(v[43] == 1125);
    assert_or_panic(v[44] == 1126);
    assert_or_panic(v[45] == 1127);
    assert_or_panic(v[46] == 1128);
    assert_or_panic(v[47] == 1129);
    assert_or_panic(v[48] == 1130);
    assert_or_panic(v[49] == 1131);
    assert_or_panic(v[50] == 1132);
    assert_or_panic(v[51] == 1133);
    assert_or_panic(v[52] == 1134);
    assert_or_panic(v[53] == 1135);
    assert_or_panic(v[54] == 1136);
    assert_or_panic(v[55] == 1137);
    assert_or_panic(v[56] == 1138);
    assert_or_panic(v[57] == 1139);
    assert_or_panic(v[58] == 1140);
    assert_or_panic(v[59] == 1141);
    assert_or_panic(v[60] == 1142);
    assert_or_panic(v[61] == 1143);
    assert_or_panic(v[62] == 1144);
    assert_or_panic(v[63] == 1145);
    assert_or_panic(v[64] == 1146);
    assert_or_panic(v[65] == 1147);
    assert_or_panic(v[66] == 1148);
    assert_or_panic(v[67] == 1149);
    assert_or_panic(v[68] == 1150);
    assert_or_panic(v[69] == 1151);
    assert_or_panic(v[70] == 1152);
    assert_or_panic(v[71] == 1153);
    assert_or_panic(v[72] == 1154);
    assert_or_panic(v[73] == 1155);
    assert_or_panic(v[74] == 1156);
    assert_or_panic(v[75] == 1157);
    assert_or_panic(v[76] == 1158);
    assert_or_panic(v[77] == 1159);
    assert_or_panic(v[78] == 1160);
    assert_or_panic(v[79] == 1161);
    assert_or_panic(v[80] == 1162);
    assert_or_panic(v[81] == 1163);
    assert_or_panic(v[82] == 1164);
    assert_or_panic(v[83] == 1165);
    assert_or_panic(v[84] == 1166);
    assert_or_panic(v[85] == 1167);
    assert_or_panic(v[86] == 1168);
    assert_or_panic(v[87] == 1169);
    assert_or_panic(v[88] == 1170);
    assert_or_panic(v[89] == 1171);
    assert_or_panic(v[90] == 1172);
    assert_or_panic(v[91] == 1173);
    assert_or_panic(v[92] == 1174);
    assert_or_panic(v[93] == 1175);
    assert_or_panic(v[94] == 1176);
    assert_or_panic(v[95] == 1177);
    c_vector_96_u16((Vector_96_u16){
        1178, 1179, 1180, 1181, 1182, 1183, 1184, 1185, 1186, 1187, 1188, 1189, 1190, 1191, 1192, 1193,
        1194, 1195, 1196, 1197, 1198, 1199, 1200, 1201, 1202, 1203, 1204, 1205, 1206, 1207, 1208, 1209,
        1210, 1211, 1212, 1213, 1214, 1215, 1216, 1217, 1218, 1219, 1220, 1221, 1222, 1223, 1224, 1225,
        1226, 1227, 1228, 1229, 1230, 1231, 1232, 1233, 1234, 1235, 1236, 1237, 1238, 1239, 1240, 1241,
        1242, 1243, 1244, 1245, 1246, 1247, 1248, 1249, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257,
        1258, 1259, 1260, 1261, 1262, 1263, 1264, 1265, 1266, 1267, 1268, 1269, 1270, 1271, 1272, 1273,
    }, 96);
    c_test_vector_96_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint16_t Vector_128_u16 __attribute__((vector_size(128 * sizeof(uint16_t))));
Vector_128_u16 zig_ret_vector_128_u16(void) {
    return (Vector_128_u16){
    1274, 1275, 1276, 1277, 1278, 1279, 1280, 1281, 1282, 1283, 1284, 1285, 1286, 1287, 1288, 1289,
    1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1298, 1299, 1300, 1301, 1302, 1303, 1304, 1305,
    1306, 1307, 1308, 1309, 1310, 1311, 1312, 1313, 1314, 1315, 1316, 1317, 1318, 1319, 1320, 1321,
    1322, 1323, 1324, 1325, 1326, 1327, 1328, 1329, 1330, 1331, 1332, 1333, 1334, 1335, 1336, 1337,
    1338, 1339, 1340, 1341, 1342, 1343, 1344, 1345, 1346, 1347, 1348, 1349, 1350, 1351, 1352, 1353,
    1354, 1355, 1356, 1357, 1358, 1359, 1360, 1361, 1362, 1363, 1364, 1365, 1366, 1367, 1368, 1369,
    1370, 1371, 1372, 1373, 1374, 1375, 1376, 1377, 1378, 1379, 1380, 1381, 1382, 1383, 1384, 1385,
    1386, 1387, 1388, 1389, 1390, 1391, 1392, 1393, 1394, 1395, 1396, 1397, 1398, 1399, 1400, 1401,
    };
}
void zig_vector_128_u16(Vector_128_u16 v, size_t i) {
    assert_or_panic(v[0] == 1402);
    assert_or_panic(v[1] == 1403);
    assert_or_panic(v[2] == 1404);
    assert_or_panic(v[3] == 1405);
    assert_or_panic(v[4] == 1406);
    assert_or_panic(v[5] == 1407);
    assert_or_panic(v[6] == 1408);
    assert_or_panic(v[7] == 1409);
    assert_or_panic(v[8] == 1410);
    assert_or_panic(v[9] == 1411);
    assert_or_panic(v[10] == 1412);
    assert_or_panic(v[11] == 1413);
    assert_or_panic(v[12] == 1414);
    assert_or_panic(v[13] == 1415);
    assert_or_panic(v[14] == 1416);
    assert_or_panic(v[15] == 1417);
    assert_or_panic(v[16] == 1418);
    assert_or_panic(v[17] == 1419);
    assert_or_panic(v[18] == 1420);
    assert_or_panic(v[19] == 1421);
    assert_or_panic(v[20] == 1422);
    assert_or_panic(v[21] == 1423);
    assert_or_panic(v[22] == 1424);
    assert_or_panic(v[23] == 1425);
    assert_or_panic(v[24] == 1426);
    assert_or_panic(v[25] == 1427);
    assert_or_panic(v[26] == 1428);
    assert_or_panic(v[27] == 1429);
    assert_or_panic(v[28] == 1430);
    assert_or_panic(v[29] == 1431);
    assert_or_panic(v[30] == 1432);
    assert_or_panic(v[31] == 1433);
    assert_or_panic(v[32] == 1434);
    assert_or_panic(v[33] == 1435);
    assert_or_panic(v[34] == 1436);
    assert_or_panic(v[35] == 1437);
    assert_or_panic(v[36] == 1438);
    assert_or_panic(v[37] == 1439);
    assert_or_panic(v[38] == 1440);
    assert_or_panic(v[39] == 1441);
    assert_or_panic(v[40] == 1442);
    assert_or_panic(v[41] == 1443);
    assert_or_panic(v[42] == 1444);
    assert_or_panic(v[43] == 1445);
    assert_or_panic(v[44] == 1446);
    assert_or_panic(v[45] == 1447);
    assert_or_panic(v[46] == 1448);
    assert_or_panic(v[47] == 1449);
    assert_or_panic(v[48] == 1450);
    assert_or_panic(v[49] == 1451);
    assert_or_panic(v[50] == 1452);
    assert_or_panic(v[51] == 1453);
    assert_or_panic(v[52] == 1454);
    assert_or_panic(v[53] == 1455);
    assert_or_panic(v[54] == 1456);
    assert_or_panic(v[55] == 1457);
    assert_or_panic(v[56] == 1458);
    assert_or_panic(v[57] == 1459);
    assert_or_panic(v[58] == 1460);
    assert_or_panic(v[59] == 1461);
    assert_or_panic(v[60] == 1462);
    assert_or_panic(v[61] == 1463);
    assert_or_panic(v[62] == 1464);
    assert_or_panic(v[63] == 1465);
    assert_or_panic(v[64] == 1466);
    assert_or_panic(v[65] == 1467);
    assert_or_panic(v[66] == 1468);
    assert_or_panic(v[67] == 1469);
    assert_or_panic(v[68] == 1470);
    assert_or_panic(v[69] == 1471);
    assert_or_panic(v[70] == 1472);
    assert_or_panic(v[71] == 1473);
    assert_or_panic(v[72] == 1474);
    assert_or_panic(v[73] == 1475);
    assert_or_panic(v[74] == 1476);
    assert_or_panic(v[75] == 1477);
    assert_or_panic(v[76] == 1478);
    assert_or_panic(v[77] == 1479);
    assert_or_panic(v[78] == 1480);
    assert_or_panic(v[79] == 1481);
    assert_or_panic(v[80] == 1482);
    assert_or_panic(v[81] == 1483);
    assert_or_panic(v[82] == 1484);
    assert_or_panic(v[83] == 1485);
    assert_or_panic(v[84] == 1486);
    assert_or_panic(v[85] == 1487);
    assert_or_panic(v[86] == 1488);
    assert_or_panic(v[87] == 1489);
    assert_or_panic(v[88] == 1490);
    assert_or_panic(v[89] == 1491);
    assert_or_panic(v[90] == 1492);
    assert_or_panic(v[91] == 1493);
    assert_or_panic(v[92] == 1494);
    assert_or_panic(v[93] == 1495);
    assert_or_panic(v[94] == 1496);
    assert_or_panic(v[95] == 1497);
    assert_or_panic(v[96] == 1498);
    assert_or_panic(v[97] == 1499);
    assert_or_panic(v[98] == 1500);
    assert_or_panic(v[99] == 1501);
    assert_or_panic(v[100] == 1502);
    assert_or_panic(v[101] == 1503);
    assert_or_panic(v[102] == 1504);
    assert_or_panic(v[103] == 1505);
    assert_or_panic(v[104] == 1506);
    assert_or_panic(v[105] == 1507);
    assert_or_panic(v[106] == 1508);
    assert_or_panic(v[107] == 1509);
    assert_or_panic(v[108] == 1510);
    assert_or_panic(v[109] == 1511);
    assert_or_panic(v[110] == 1512);
    assert_or_panic(v[111] == 1513);
    assert_or_panic(v[112] == 1514);
    assert_or_panic(v[113] == 1515);
    assert_or_panic(v[114] == 1516);
    assert_or_panic(v[115] == 1517);
    assert_or_panic(v[116] == 1518);
    assert_or_panic(v[117] == 1519);
    assert_or_panic(v[118] == 1520);
    assert_or_panic(v[119] == 1521);
    assert_or_panic(v[120] == 1522);
    assert_or_panic(v[121] == 1523);
    assert_or_panic(v[122] == 1524);
    assert_or_panic(v[123] == 1525);
    assert_or_panic(v[124] == 1526);
    assert_or_panic(v[125] == 1527);
    assert_or_panic(v[126] == 1528);
    assert_or_panic(v[127] == 1529);
    assert_or_panic(i == 128);
}
Vector_128_u16 c_ret_vector_128_u16(void);
void c_vector_128_u16(Vector_128_u16, size_t);
void c_test_vector_128_u16(void);
static void test_vector_128_u16(void) {
    c_abi_current_test = "@Vector(128, u16)";
    Vector_128_u16 v = c_ret_vector_128_u16();
    assert_or_panic(v[0] == 1530);
    assert_or_panic(v[1] == 1531);
    assert_or_panic(v[2] == 1532);
    assert_or_panic(v[3] == 1533);
    assert_or_panic(v[4] == 1534);
    assert_or_panic(v[5] == 1535);
    assert_or_panic(v[6] == 1536);
    assert_or_panic(v[7] == 1537);
    assert_or_panic(v[8] == 1538);
    assert_or_panic(v[9] == 1539);
    assert_or_panic(v[10] == 1540);
    assert_or_panic(v[11] == 1541);
    assert_or_panic(v[12] == 1542);
    assert_or_panic(v[13] == 1543);
    assert_or_panic(v[14] == 1544);
    assert_or_panic(v[15] == 1545);
    assert_or_panic(v[16] == 1546);
    assert_or_panic(v[17] == 1547);
    assert_or_panic(v[18] == 1548);
    assert_or_panic(v[19] == 1549);
    assert_or_panic(v[20] == 1550);
    assert_or_panic(v[21] == 1551);
    assert_or_panic(v[22] == 1552);
    assert_or_panic(v[23] == 1553);
    assert_or_panic(v[24] == 1554);
    assert_or_panic(v[25] == 1555);
    assert_or_panic(v[26] == 1556);
    assert_or_panic(v[27] == 1557);
    assert_or_panic(v[28] == 1558);
    assert_or_panic(v[29] == 1559);
    assert_or_panic(v[30] == 1560);
    assert_or_panic(v[31] == 1561);
    assert_or_panic(v[32] == 1562);
    assert_or_panic(v[33] == 1563);
    assert_or_panic(v[34] == 1564);
    assert_or_panic(v[35] == 1565);
    assert_or_panic(v[36] == 1566);
    assert_or_panic(v[37] == 1567);
    assert_or_panic(v[38] == 1568);
    assert_or_panic(v[39] == 1569);
    assert_or_panic(v[40] == 1570);
    assert_or_panic(v[41] == 1571);
    assert_or_panic(v[42] == 1572);
    assert_or_panic(v[43] == 1573);
    assert_or_panic(v[44] == 1574);
    assert_or_panic(v[45] == 1575);
    assert_or_panic(v[46] == 1576);
    assert_or_panic(v[47] == 1577);
    assert_or_panic(v[48] == 1578);
    assert_or_panic(v[49] == 1579);
    assert_or_panic(v[50] == 1580);
    assert_or_panic(v[51] == 1581);
    assert_or_panic(v[52] == 1582);
    assert_or_panic(v[53] == 1583);
    assert_or_panic(v[54] == 1584);
    assert_or_panic(v[55] == 1585);
    assert_or_panic(v[56] == 1586);
    assert_or_panic(v[57] == 1587);
    assert_or_panic(v[58] == 1588);
    assert_or_panic(v[59] == 1589);
    assert_or_panic(v[60] == 1590);
    assert_or_panic(v[61] == 1591);
    assert_or_panic(v[62] == 1592);
    assert_or_panic(v[63] == 1593);
    assert_or_panic(v[64] == 1594);
    assert_or_panic(v[65] == 1595);
    assert_or_panic(v[66] == 1596);
    assert_or_panic(v[67] == 1597);
    assert_or_panic(v[68] == 1598);
    assert_or_panic(v[69] == 1599);
    assert_or_panic(v[70] == 1600);
    assert_or_panic(v[71] == 1601);
    assert_or_panic(v[72] == 1602);
    assert_or_panic(v[73] == 1603);
    assert_or_panic(v[74] == 1604);
    assert_or_panic(v[75] == 1605);
    assert_or_panic(v[76] == 1606);
    assert_or_panic(v[77] == 1607);
    assert_or_panic(v[78] == 1608);
    assert_or_panic(v[79] == 1609);
    assert_or_panic(v[80] == 1610);
    assert_or_panic(v[81] == 1611);
    assert_or_panic(v[82] == 1612);
    assert_or_panic(v[83] == 1613);
    assert_or_panic(v[84] == 1614);
    assert_or_panic(v[85] == 1615);
    assert_or_panic(v[86] == 1616);
    assert_or_panic(v[87] == 1617);
    assert_or_panic(v[88] == 1618);
    assert_or_panic(v[89] == 1619);
    assert_or_panic(v[90] == 1620);
    assert_or_panic(v[91] == 1621);
    assert_or_panic(v[92] == 1622);
    assert_or_panic(v[93] == 1623);
    assert_or_panic(v[94] == 1624);
    assert_or_panic(v[95] == 1625);
    assert_or_panic(v[96] == 1626);
    assert_or_panic(v[97] == 1627);
    assert_or_panic(v[98] == 1628);
    assert_or_panic(v[99] == 1629);
    assert_or_panic(v[100] == 1630);
    assert_or_panic(v[101] == 1631);
    assert_or_panic(v[102] == 1632);
    assert_or_panic(v[103] == 1633);
    assert_or_panic(v[104] == 1634);
    assert_or_panic(v[105] == 1635);
    assert_or_panic(v[106] == 1636);
    assert_or_panic(v[107] == 1637);
    assert_or_panic(v[108] == 1638);
    assert_or_panic(v[109] == 1639);
    assert_or_panic(v[110] == 1640);
    assert_or_panic(v[111] == 1641);
    assert_or_panic(v[112] == 1642);
    assert_or_panic(v[113] == 1643);
    assert_or_panic(v[114] == 1644);
    assert_or_panic(v[115] == 1645);
    assert_or_panic(v[116] == 1646);
    assert_or_panic(v[117] == 1647);
    assert_or_panic(v[118] == 1648);
    assert_or_panic(v[119] == 1649);
    assert_or_panic(v[120] == 1650);
    assert_or_panic(v[121] == 1651);
    assert_or_panic(v[122] == 1652);
    assert_or_panic(v[123] == 1653);
    assert_or_panic(v[124] == 1654);
    assert_or_panic(v[125] == 1655);
    assert_or_panic(v[126] == 1656);
    assert_or_panic(v[127] == 1657);
    c_vector_128_u16((Vector_128_u16){
        1658, 1659, 1660, 1661, 1662, 1663, 1664, 1665, 1666, 1667, 1668, 1669, 1670, 1671, 1672, 1673,
        1674, 1675, 1676, 1677, 1678, 1679, 1680, 1681, 1682, 1683, 1684, 1685, 1686, 1687, 1688, 1689,
        1690, 1691, 1692, 1693, 1694, 1695, 1696, 1697, 1698, 1699, 1700, 1701, 1702, 1703, 1704, 1705,
        1706, 1707, 1708, 1709, 1710, 1711, 1712, 1713, 1714, 1715, 1716, 1717, 1718, 1719, 1720, 1721,
        1722, 1723, 1724, 1725, 1726, 1727, 1728, 1729, 1730, 1731, 1732, 1733, 1734, 1735, 1736, 1737,
        1738, 1739, 1740, 1741, 1742, 1743, 1744, 1745, 1746, 1747, 1748, 1749, 1750, 1751, 1752, 1753,
        1754, 1755, 1756, 1757, 1758, 1759, 1760, 1761, 1762, 1763, 1764, 1765, 1766, 1767, 1768, 1769,
        1770, 1771, 1772, 1773, 1774, 1775, 1776, 1777, 1778, 1779, 1780, 1781, 1782, 1783, 1784, 1785,
    }, 128);
    c_test_vector_128_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint16_t Vector_192_u16 __attribute__((vector_size(192 * sizeof(uint16_t))));
Vector_192_u16 zig_ret_vector_192_u16(void) {
    return (Vector_192_u16){
    1786, 1787, 1788, 1789, 1790, 1791, 1792, 1793, 1794, 1795, 1796, 1797, 1798, 1799, 1800, 1801,
    1802, 1803, 1804, 1805, 1806, 1807, 1808, 1809, 1810, 1811, 1812, 1813, 1814, 1815, 1816, 1817,
    1818, 1819, 1820, 1821, 1822, 1823, 1824, 1825, 1826, 1827, 1828, 1829, 1830, 1831, 1832, 1833,
    1834, 1835, 1836, 1837, 1838, 1839, 1840, 1841, 1842, 1843, 1844, 1845, 1846, 1847, 1848, 1849,
    1850, 1851, 1852, 1853, 1854, 1855, 1856, 1857, 1858, 1859, 1860, 1861, 1862, 1863, 1864, 1865,
    1866, 1867, 1868, 1869, 1870, 1871, 1872, 1873, 1874, 1875, 1876, 1877, 1878, 1879, 1880, 1881,
    1882, 1883, 1884, 1885, 1886, 1887, 1888, 1889, 1890, 1891, 1892, 1893, 1894, 1895, 1896, 1897,
    1898, 1899, 1900, 1901, 1902, 1903, 1904, 1905, 1906, 1907, 1908, 1909, 1910, 1911, 1912, 1913,
    1914, 1915, 1916, 1917, 1918, 1919, 1920, 1921, 1922, 1923, 1924, 1925, 1926, 1927, 1928, 1929,
    1930, 1931, 1932, 1933, 1934, 1935, 1936, 1937, 1938, 1939, 1940, 1941, 1942, 1943, 1944, 1945,
    1946, 1947, 1948, 1949, 1950, 1951, 1952, 1953, 1954, 1955, 1956, 1957, 1958, 1959, 1960, 1961,
    1962, 1963, 1964, 1965, 1966, 1967, 1968, 1969, 1970, 1971, 1972, 1973, 1974, 1975, 1976, 1977,
    };
}
void zig_vector_192_u16(Vector_192_u16 v, size_t i) {
    assert_or_panic(v[0] == 1978);
    assert_or_panic(v[1] == 1979);
    assert_or_panic(v[2] == 1980);
    assert_or_panic(v[3] == 1981);
    assert_or_panic(v[4] == 1982);
    assert_or_panic(v[5] == 1983);
    assert_or_panic(v[6] == 1984);
    assert_or_panic(v[7] == 1985);
    assert_or_panic(v[8] == 1986);
    assert_or_panic(v[9] == 1987);
    assert_or_panic(v[10] == 1988);
    assert_or_panic(v[11] == 1989);
    assert_or_panic(v[12] == 1990);
    assert_or_panic(v[13] == 1991);
    assert_or_panic(v[14] == 1992);
    assert_or_panic(v[15] == 1993);
    assert_or_panic(v[16] == 1994);
    assert_or_panic(v[17] == 1995);
    assert_or_panic(v[18] == 1996);
    assert_or_panic(v[19] == 1997);
    assert_or_panic(v[20] == 1998);
    assert_or_panic(v[21] == 1999);
    assert_or_panic(v[22] == 2000);
    assert_or_panic(v[23] == 2001);
    assert_or_panic(v[24] == 2002);
    assert_or_panic(v[25] == 2003);
    assert_or_panic(v[26] == 2004);
    assert_or_panic(v[27] == 2005);
    assert_or_panic(v[28] == 2006);
    assert_or_panic(v[29] == 2007);
    assert_or_panic(v[30] == 2008);
    assert_or_panic(v[31] == 2009);
    assert_or_panic(v[32] == 2010);
    assert_or_panic(v[33] == 2011);
    assert_or_panic(v[34] == 2012);
    assert_or_panic(v[35] == 2013);
    assert_or_panic(v[36] == 2014);
    assert_or_panic(v[37] == 2015);
    assert_or_panic(v[38] == 2016);
    assert_or_panic(v[39] == 2017);
    assert_or_panic(v[40] == 2018);
    assert_or_panic(v[41] == 2019);
    assert_or_panic(v[42] == 2020);
    assert_or_panic(v[43] == 2021);
    assert_or_panic(v[44] == 2022);
    assert_or_panic(v[45] == 2023);
    assert_or_panic(v[46] == 2024);
    assert_or_panic(v[47] == 2025);
    assert_or_panic(v[48] == 2026);
    assert_or_panic(v[49] == 2027);
    assert_or_panic(v[50] == 2028);
    assert_or_panic(v[51] == 2029);
    assert_or_panic(v[52] == 2030);
    assert_or_panic(v[53] == 2031);
    assert_or_panic(v[54] == 2032);
    assert_or_panic(v[55] == 2033);
    assert_or_panic(v[56] == 2034);
    assert_or_panic(v[57] == 2035);
    assert_or_panic(v[58] == 2036);
    assert_or_panic(v[59] == 2037);
    assert_or_panic(v[60] == 2038);
    assert_or_panic(v[61] == 2039);
    assert_or_panic(v[62] == 2040);
    assert_or_panic(v[63] == 2041);
    assert_or_panic(v[64] == 2042);
    assert_or_panic(v[65] == 2043);
    assert_or_panic(v[66] == 2044);
    assert_or_panic(v[67] == 2045);
    assert_or_panic(v[68] == 2046);
    assert_or_panic(v[69] == 2047);
    assert_or_panic(v[70] == 2048);
    assert_or_panic(v[71] == 2049);
    assert_or_panic(v[72] == 2050);
    assert_or_panic(v[73] == 2051);
    assert_or_panic(v[74] == 2052);
    assert_or_panic(v[75] == 2053);
    assert_or_panic(v[76] == 2054);
    assert_or_panic(v[77] == 2055);
    assert_or_panic(v[78] == 2056);
    assert_or_panic(v[79] == 2057);
    assert_or_panic(v[80] == 2058);
    assert_or_panic(v[81] == 2059);
    assert_or_panic(v[82] == 2060);
    assert_or_panic(v[83] == 2061);
    assert_or_panic(v[84] == 2062);
    assert_or_panic(v[85] == 2063);
    assert_or_panic(v[86] == 2064);
    assert_or_panic(v[87] == 2065);
    assert_or_panic(v[88] == 2066);
    assert_or_panic(v[89] == 2067);
    assert_or_panic(v[90] == 2068);
    assert_or_panic(v[91] == 2069);
    assert_or_panic(v[92] == 2070);
    assert_or_panic(v[93] == 2071);
    assert_or_panic(v[94] == 2072);
    assert_or_panic(v[95] == 2073);
    assert_or_panic(v[96] == 2074);
    assert_or_panic(v[97] == 2075);
    assert_or_panic(v[98] == 2076);
    assert_or_panic(v[99] == 2077);
    assert_or_panic(v[100] == 2078);
    assert_or_panic(v[101] == 2079);
    assert_or_panic(v[102] == 2080);
    assert_or_panic(v[103] == 2081);
    assert_or_panic(v[104] == 2082);
    assert_or_panic(v[105] == 2083);
    assert_or_panic(v[106] == 2084);
    assert_or_panic(v[107] == 2085);
    assert_or_panic(v[108] == 2086);
    assert_or_panic(v[109] == 2087);
    assert_or_panic(v[110] == 2088);
    assert_or_panic(v[111] == 2089);
    assert_or_panic(v[112] == 2090);
    assert_or_panic(v[113] == 2091);
    assert_or_panic(v[114] == 2092);
    assert_or_panic(v[115] == 2093);
    assert_or_panic(v[116] == 2094);
    assert_or_panic(v[117] == 2095);
    assert_or_panic(v[118] == 2096);
    assert_or_panic(v[119] == 2097);
    assert_or_panic(v[120] == 2098);
    assert_or_panic(v[121] == 2099);
    assert_or_panic(v[122] == 2100);
    assert_or_panic(v[123] == 2101);
    assert_or_panic(v[124] == 2102);
    assert_or_panic(v[125] == 2103);
    assert_or_panic(v[126] == 2104);
    assert_or_panic(v[127] == 2105);
    assert_or_panic(v[128] == 2106);
    assert_or_panic(v[129] == 2107);
    assert_or_panic(v[130] == 2108);
    assert_or_panic(v[131] == 2109);
    assert_or_panic(v[132] == 2110);
    assert_or_panic(v[133] == 2111);
    assert_or_panic(v[134] == 2112);
    assert_or_panic(v[135] == 2113);
    assert_or_panic(v[136] == 2114);
    assert_or_panic(v[137] == 2115);
    assert_or_panic(v[138] == 2116);
    assert_or_panic(v[139] == 2117);
    assert_or_panic(v[140] == 2118);
    assert_or_panic(v[141] == 2119);
    assert_or_panic(v[142] == 2120);
    assert_or_panic(v[143] == 2121);
    assert_or_panic(v[144] == 2122);
    assert_or_panic(v[145] == 2123);
    assert_or_panic(v[146] == 2124);
    assert_or_panic(v[147] == 2125);
    assert_or_panic(v[148] == 2126);
    assert_or_panic(v[149] == 2127);
    assert_or_panic(v[150] == 2128);
    assert_or_panic(v[151] == 2129);
    assert_or_panic(v[152] == 2130);
    assert_or_panic(v[153] == 2131);
    assert_or_panic(v[154] == 2132);
    assert_or_panic(v[155] == 2133);
    assert_or_panic(v[156] == 2134);
    assert_or_panic(v[157] == 2135);
    assert_or_panic(v[158] == 2136);
    assert_or_panic(v[159] == 2137);
    assert_or_panic(v[160] == 2138);
    assert_or_panic(v[161] == 2139);
    assert_or_panic(v[162] == 2140);
    assert_or_panic(v[163] == 2141);
    assert_or_panic(v[164] == 2142);
    assert_or_panic(v[165] == 2143);
    assert_or_panic(v[166] == 2144);
    assert_or_panic(v[167] == 2145);
    assert_or_panic(v[168] == 2146);
    assert_or_panic(v[169] == 2147);
    assert_or_panic(v[170] == 2148);
    assert_or_panic(v[171] == 2149);
    assert_or_panic(v[172] == 2150);
    assert_or_panic(v[173] == 2151);
    assert_or_panic(v[174] == 2152);
    assert_or_panic(v[175] == 2153);
    assert_or_panic(v[176] == 2154);
    assert_or_panic(v[177] == 2155);
    assert_or_panic(v[178] == 2156);
    assert_or_panic(v[179] == 2157);
    assert_or_panic(v[180] == 2158);
    assert_or_panic(v[181] == 2159);
    assert_or_panic(v[182] == 2160);
    assert_or_panic(v[183] == 2161);
    assert_or_panic(v[184] == 2162);
    assert_or_panic(v[185] == 2163);
    assert_or_panic(v[186] == 2164);
    assert_or_panic(v[187] == 2165);
    assert_or_panic(v[188] == 2166);
    assert_or_panic(v[189] == 2167);
    assert_or_panic(v[190] == 2168);
    assert_or_panic(v[191] == 2169);
    assert_or_panic(i == 192);
}
Vector_192_u16 c_ret_vector_192_u16(void);
void c_vector_192_u16(Vector_192_u16, size_t);
void c_test_vector_192_u16(void);
static void test_vector_192_u16(void) {
    c_abi_current_test = "@Vector(192, u16)";
    Vector_192_u16 v = c_ret_vector_192_u16();
    assert_or_panic(v[0] == 2170);
    assert_or_panic(v[1] == 2171);
    assert_or_panic(v[2] == 2172);
    assert_or_panic(v[3] == 2173);
    assert_or_panic(v[4] == 2174);
    assert_or_panic(v[5] == 2175);
    assert_or_panic(v[6] == 2176);
    assert_or_panic(v[7] == 2177);
    assert_or_panic(v[8] == 2178);
    assert_or_panic(v[9] == 2179);
    assert_or_panic(v[10] == 2180);
    assert_or_panic(v[11] == 2181);
    assert_or_panic(v[12] == 2182);
    assert_or_panic(v[13] == 2183);
    assert_or_panic(v[14] == 2184);
    assert_or_panic(v[15] == 2185);
    assert_or_panic(v[16] == 2186);
    assert_or_panic(v[17] == 2187);
    assert_or_panic(v[18] == 2188);
    assert_or_panic(v[19] == 2189);
    assert_or_panic(v[20] == 2190);
    assert_or_panic(v[21] == 2191);
    assert_or_panic(v[22] == 2192);
    assert_or_panic(v[23] == 2193);
    assert_or_panic(v[24] == 2194);
    assert_or_panic(v[25] == 2195);
    assert_or_panic(v[26] == 2196);
    assert_or_panic(v[27] == 2197);
    assert_or_panic(v[28] == 2198);
    assert_or_panic(v[29] == 2199);
    assert_or_panic(v[30] == 2200);
    assert_or_panic(v[31] == 2201);
    assert_or_panic(v[32] == 2202);
    assert_or_panic(v[33] == 2203);
    assert_or_panic(v[34] == 2204);
    assert_or_panic(v[35] == 2205);
    assert_or_panic(v[36] == 2206);
    assert_or_panic(v[37] == 2207);
    assert_or_panic(v[38] == 2208);
    assert_or_panic(v[39] == 2209);
    assert_or_panic(v[40] == 2210);
    assert_or_panic(v[41] == 2211);
    assert_or_panic(v[42] == 2212);
    assert_or_panic(v[43] == 2213);
    assert_or_panic(v[44] == 2214);
    assert_or_panic(v[45] == 2215);
    assert_or_panic(v[46] == 2216);
    assert_or_panic(v[47] == 2217);
    assert_or_panic(v[48] == 2218);
    assert_or_panic(v[49] == 2219);
    assert_or_panic(v[50] == 2220);
    assert_or_panic(v[51] == 2221);
    assert_or_panic(v[52] == 2222);
    assert_or_panic(v[53] == 2223);
    assert_or_panic(v[54] == 2224);
    assert_or_panic(v[55] == 2225);
    assert_or_panic(v[56] == 2226);
    assert_or_panic(v[57] == 2227);
    assert_or_panic(v[58] == 2228);
    assert_or_panic(v[59] == 2229);
    assert_or_panic(v[60] == 2230);
    assert_or_panic(v[61] == 2231);
    assert_or_panic(v[62] == 2232);
    assert_or_panic(v[63] == 2233);
    assert_or_panic(v[64] == 2234);
    assert_or_panic(v[65] == 2235);
    assert_or_panic(v[66] == 2236);
    assert_or_panic(v[67] == 2237);
    assert_or_panic(v[68] == 2238);
    assert_or_panic(v[69] == 2239);
    assert_or_panic(v[70] == 2240);
    assert_or_panic(v[71] == 2241);
    assert_or_panic(v[72] == 2242);
    assert_or_panic(v[73] == 2243);
    assert_or_panic(v[74] == 2244);
    assert_or_panic(v[75] == 2245);
    assert_or_panic(v[76] == 2246);
    assert_or_panic(v[77] == 2247);
    assert_or_panic(v[78] == 2248);
    assert_or_panic(v[79] == 2249);
    assert_or_panic(v[80] == 2250);
    assert_or_panic(v[81] == 2251);
    assert_or_panic(v[82] == 2252);
    assert_or_panic(v[83] == 2253);
    assert_or_panic(v[84] == 2254);
    assert_or_panic(v[85] == 2255);
    assert_or_panic(v[86] == 2256);
    assert_or_panic(v[87] == 2257);
    assert_or_panic(v[88] == 2258);
    assert_or_panic(v[89] == 2259);
    assert_or_panic(v[90] == 2260);
    assert_or_panic(v[91] == 2261);
    assert_or_panic(v[92] == 2262);
    assert_or_panic(v[93] == 2263);
    assert_or_panic(v[94] == 2264);
    assert_or_panic(v[95] == 2265);
    assert_or_panic(v[96] == 2266);
    assert_or_panic(v[97] == 2267);
    assert_or_panic(v[98] == 2268);
    assert_or_panic(v[99] == 2269);
    assert_or_panic(v[100] == 2270);
    assert_or_panic(v[101] == 2271);
    assert_or_panic(v[102] == 2272);
    assert_or_panic(v[103] == 2273);
    assert_or_panic(v[104] == 2274);
    assert_or_panic(v[105] == 2275);
    assert_or_panic(v[106] == 2276);
    assert_or_panic(v[107] == 2277);
    assert_or_panic(v[108] == 2278);
    assert_or_panic(v[109] == 2279);
    assert_or_panic(v[110] == 2280);
    assert_or_panic(v[111] == 2281);
    assert_or_panic(v[112] == 2282);
    assert_or_panic(v[113] == 2283);
    assert_or_panic(v[114] == 2284);
    assert_or_panic(v[115] == 2285);
    assert_or_panic(v[116] == 2286);
    assert_or_panic(v[117] == 2287);
    assert_or_panic(v[118] == 2288);
    assert_or_panic(v[119] == 2289);
    assert_or_panic(v[120] == 2290);
    assert_or_panic(v[121] == 2291);
    assert_or_panic(v[122] == 2292);
    assert_or_panic(v[123] == 2293);
    assert_or_panic(v[124] == 2294);
    assert_or_panic(v[125] == 2295);
    assert_or_panic(v[126] == 2296);
    assert_or_panic(v[127] == 2297);
    assert_or_panic(v[128] == 2298);
    assert_or_panic(v[129] == 2299);
    assert_or_panic(v[130] == 2300);
    assert_or_panic(v[131] == 2301);
    assert_or_panic(v[132] == 2302);
    assert_or_panic(v[133] == 2303);
    assert_or_panic(v[134] == 2304);
    assert_or_panic(v[135] == 2305);
    assert_or_panic(v[136] == 2306);
    assert_or_panic(v[137] == 2307);
    assert_or_panic(v[138] == 2308);
    assert_or_panic(v[139] == 2309);
    assert_or_panic(v[140] == 2310);
    assert_or_panic(v[141] == 2311);
    assert_or_panic(v[142] == 2312);
    assert_or_panic(v[143] == 2313);
    assert_or_panic(v[144] == 2314);
    assert_or_panic(v[145] == 2315);
    assert_or_panic(v[146] == 2316);
    assert_or_panic(v[147] == 2317);
    assert_or_panic(v[148] == 2318);
    assert_or_panic(v[149] == 2319);
    assert_or_panic(v[150] == 2320);
    assert_or_panic(v[151] == 2321);
    assert_or_panic(v[152] == 2322);
    assert_or_panic(v[153] == 2323);
    assert_or_panic(v[154] == 2324);
    assert_or_panic(v[155] == 2325);
    assert_or_panic(v[156] == 2326);
    assert_or_panic(v[157] == 2327);
    assert_or_panic(v[158] == 2328);
    assert_or_panic(v[159] == 2329);
    assert_or_panic(v[160] == 2330);
    assert_or_panic(v[161] == 2331);
    assert_or_panic(v[162] == 2332);
    assert_or_panic(v[163] == 2333);
    assert_or_panic(v[164] == 2334);
    assert_or_panic(v[165] == 2335);
    assert_or_panic(v[166] == 2336);
    assert_or_panic(v[167] == 2337);
    assert_or_panic(v[168] == 2338);
    assert_or_panic(v[169] == 2339);
    assert_or_panic(v[170] == 2340);
    assert_or_panic(v[171] == 2341);
    assert_or_panic(v[172] == 2342);
    assert_or_panic(v[173] == 2343);
    assert_or_panic(v[174] == 2344);
    assert_or_panic(v[175] == 2345);
    assert_or_panic(v[176] == 2346);
    assert_or_panic(v[177] == 2347);
    assert_or_panic(v[178] == 2348);
    assert_or_panic(v[179] == 2349);
    assert_or_panic(v[180] == 2350);
    assert_or_panic(v[181] == 2351);
    assert_or_panic(v[182] == 2352);
    assert_or_panic(v[183] == 2353);
    assert_or_panic(v[184] == 2354);
    assert_or_panic(v[185] == 2355);
    assert_or_panic(v[186] == 2356);
    assert_or_panic(v[187] == 2357);
    assert_or_panic(v[188] == 2358);
    assert_or_panic(v[189] == 2359);
    assert_or_panic(v[190] == 2360);
    assert_or_panic(v[191] == 2361);
    c_vector_192_u16((Vector_192_u16){
        2362, 2363, 2364, 2365, 2366, 2367, 2368, 2369, 2370, 2371, 2372, 2373, 2374, 2375, 2376, 2377,
        2378, 2379, 2380, 2381, 2382, 2383, 2384, 2385, 2386, 2387, 2388, 2389, 2390, 2391, 2392, 2393,
        2394, 2395, 2396, 2397, 2398, 2399, 2400, 2401, 2402, 2403, 2404, 2405, 2406, 2407, 2408, 2409,
        2410, 2411, 2412, 2413, 2414, 2415, 2416, 2417, 2418, 2419, 2420, 2421, 2422, 2423, 2424, 2425,
        2426, 2427, 2428, 2429, 2430, 2431, 2432, 2433, 2434, 2435, 2436, 2437, 2438, 2439, 2440, 2441,
        2442, 2443, 2444, 2445, 2446, 2447, 2448, 2449, 2450, 2451, 2452, 2453, 2454, 2455, 2456, 2457,
        2458, 2459, 2460, 2461, 2462, 2463, 2464, 2465, 2466, 2467, 2468, 2469, 2470, 2471, 2472, 2473,
        2474, 2475, 2476, 2477, 2478, 2479, 2480, 2481, 2482, 2483, 2484, 2485, 2486, 2487, 2488, 2489,
        2490, 2491, 2492, 2493, 2494, 2495, 2496, 2497, 2498, 2499, 2500, 2501, 2502, 2503, 2504, 2505,
        2506, 2507, 2508, 2509, 2510, 2511, 2512, 2513, 2514, 2515, 2516, 2517, 2518, 2519, 2520, 2521,
        2522, 2523, 2524, 2525, 2526, 2527, 2528, 2529, 2530, 2531, 2532, 2533, 2534, 2535, 2536, 2537,
        2538, 2539, 2540, 2541, 2542, 2543, 2544, 2545, 2546, 2547, 2548, 2549, 2550, 2551, 2552, 2553,
    }, 192);
    c_test_vector_192_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint16_t Vector_256_u16 __attribute__((vector_size(256 * sizeof(uint16_t))));
Vector_256_u16 zig_ret_vector_256_u16(void) {
    return (Vector_256_u16){
    2554, 2555, 2556, 2557, 2558, 2559, 2560, 2561, 2562, 2563, 2564, 2565, 2566, 2567, 2568, 2569,
    2570, 2571, 2572, 2573, 2574, 2575, 2576, 2577, 2578, 2579, 2580, 2581, 2582, 2583, 2584, 2585,
    2586, 2587, 2588, 2589, 2590, 2591, 2592, 2593, 2594, 2595, 2596, 2597, 2598, 2599, 2600, 2601,
    2602, 2603, 2604, 2605, 2606, 2607, 2608, 2609, 2610, 2611, 2612, 2613, 2614, 2615, 2616, 2617,
    2618, 2619, 2620, 2621, 2622, 2623, 2624, 2625, 2626, 2627, 2628, 2629, 2630, 2631, 2632, 2633,
    2634, 2635, 2636, 2637, 2638, 2639, 2640, 2641, 2642, 2643, 2644, 2645, 2646, 2647, 2648, 2649,
    2650, 2651, 2652, 2653, 2654, 2655, 2656, 2657, 2658, 2659, 2660, 2661, 2662, 2663, 2664, 2665,
    2666, 2667, 2668, 2669, 2670, 2671, 2672, 2673, 2674, 2675, 2676, 2677, 2678, 2679, 2680, 2681,
    2682, 2683, 2684, 2685, 2686, 2687, 2688, 2689, 2690, 2691, 2692, 2693, 2694, 2695, 2696, 2697,
    2698, 2699, 2700, 2701, 2702, 2703, 2704, 2705, 2706, 2707, 2708, 2709, 2710, 2711, 2712, 2713,
    2714, 2715, 2716, 2717, 2718, 2719, 2720, 2721, 2722, 2723, 2724, 2725, 2726, 2727, 2728, 2729,
    2730, 2731, 2732, 2733, 2734, 2735, 2736, 2737, 2738, 2739, 2740, 2741, 2742, 2743, 2744, 2745,
    2746, 2747, 2748, 2749, 2750, 2751, 2752, 2753, 2754, 2755, 2756, 2757, 2758, 2759, 2760, 2761,
    2762, 2763, 2764, 2765, 2766, 2767, 2768, 2769, 2770, 2771, 2772, 2773, 2774, 2775, 2776, 2777,
    2778, 2779, 2780, 2781, 2782, 2783, 2784, 2785, 2786, 2787, 2788, 2789, 2790, 2791, 2792, 2793,
    2794, 2795, 2796, 2797, 2798, 2799, 2800, 2801, 2802, 2803, 2804, 2805, 2806, 2807, 2808, 2809,
    };
}
void zig_vector_256_u16(Vector_256_u16 v, size_t i) {
    assert_or_panic(v[0] == 2810);
    assert_or_panic(v[1] == 2811);
    assert_or_panic(v[2] == 2812);
    assert_or_panic(v[3] == 2813);
    assert_or_panic(v[4] == 2814);
    assert_or_panic(v[5] == 2815);
    assert_or_panic(v[6] == 2816);
    assert_or_panic(v[7] == 2817);
    assert_or_panic(v[8] == 2818);
    assert_or_panic(v[9] == 2819);
    assert_or_panic(v[10] == 2820);
    assert_or_panic(v[11] == 2821);
    assert_or_panic(v[12] == 2822);
    assert_or_panic(v[13] == 2823);
    assert_or_panic(v[14] == 2824);
    assert_or_panic(v[15] == 2825);
    assert_or_panic(v[16] == 2826);
    assert_or_panic(v[17] == 2827);
    assert_or_panic(v[18] == 2828);
    assert_or_panic(v[19] == 2829);
    assert_or_panic(v[20] == 2830);
    assert_or_panic(v[21] == 2831);
    assert_or_panic(v[22] == 2832);
    assert_or_panic(v[23] == 2833);
    assert_or_panic(v[24] == 2834);
    assert_or_panic(v[25] == 2835);
    assert_or_panic(v[26] == 2836);
    assert_or_panic(v[27] == 2837);
    assert_or_panic(v[28] == 2838);
    assert_or_panic(v[29] == 2839);
    assert_or_panic(v[30] == 2840);
    assert_or_panic(v[31] == 2841);
    assert_or_panic(v[32] == 2842);
    assert_or_panic(v[33] == 2843);
    assert_or_panic(v[34] == 2844);
    assert_or_panic(v[35] == 2845);
    assert_or_panic(v[36] == 2846);
    assert_or_panic(v[37] == 2847);
    assert_or_panic(v[38] == 2848);
    assert_or_panic(v[39] == 2849);
    assert_or_panic(v[40] == 2850);
    assert_or_panic(v[41] == 2851);
    assert_or_panic(v[42] == 2852);
    assert_or_panic(v[43] == 2853);
    assert_or_panic(v[44] == 2854);
    assert_or_panic(v[45] == 2855);
    assert_or_panic(v[46] == 2856);
    assert_or_panic(v[47] == 2857);
    assert_or_panic(v[48] == 2858);
    assert_or_panic(v[49] == 2859);
    assert_or_panic(v[50] == 2860);
    assert_or_panic(v[51] == 2861);
    assert_or_panic(v[52] == 2862);
    assert_or_panic(v[53] == 2863);
    assert_or_panic(v[54] == 2864);
    assert_or_panic(v[55] == 2865);
    assert_or_panic(v[56] == 2866);
    assert_or_panic(v[57] == 2867);
    assert_or_panic(v[58] == 2868);
    assert_or_panic(v[59] == 2869);
    assert_or_panic(v[60] == 2870);
    assert_or_panic(v[61] == 2871);
    assert_or_panic(v[62] == 2872);
    assert_or_panic(v[63] == 2873);
    assert_or_panic(v[64] == 2874);
    assert_or_panic(v[65] == 2875);
    assert_or_panic(v[66] == 2876);
    assert_or_panic(v[67] == 2877);
    assert_or_panic(v[68] == 2878);
    assert_or_panic(v[69] == 2879);
    assert_or_panic(v[70] == 2880);
    assert_or_panic(v[71] == 2881);
    assert_or_panic(v[72] == 2882);
    assert_or_panic(v[73] == 2883);
    assert_or_panic(v[74] == 2884);
    assert_or_panic(v[75] == 2885);
    assert_or_panic(v[76] == 2886);
    assert_or_panic(v[77] == 2887);
    assert_or_panic(v[78] == 2888);
    assert_or_panic(v[79] == 2889);
    assert_or_panic(v[80] == 2890);
    assert_or_panic(v[81] == 2891);
    assert_or_panic(v[82] == 2892);
    assert_or_panic(v[83] == 2893);
    assert_or_panic(v[84] == 2894);
    assert_or_panic(v[85] == 2895);
    assert_or_panic(v[86] == 2896);
    assert_or_panic(v[87] == 2897);
    assert_or_panic(v[88] == 2898);
    assert_or_panic(v[89] == 2899);
    assert_or_panic(v[90] == 2900);
    assert_or_panic(v[91] == 2901);
    assert_or_panic(v[92] == 2902);
    assert_or_panic(v[93] == 2903);
    assert_or_panic(v[94] == 2904);
    assert_or_panic(v[95] == 2905);
    assert_or_panic(v[96] == 2906);
    assert_or_panic(v[97] == 2907);
    assert_or_panic(v[98] == 2908);
    assert_or_panic(v[99] == 2909);
    assert_or_panic(v[100] == 2910);
    assert_or_panic(v[101] == 2911);
    assert_or_panic(v[102] == 2912);
    assert_or_panic(v[103] == 2913);
    assert_or_panic(v[104] == 2914);
    assert_or_panic(v[105] == 2915);
    assert_or_panic(v[106] == 2916);
    assert_or_panic(v[107] == 2917);
    assert_or_panic(v[108] == 2918);
    assert_or_panic(v[109] == 2919);
    assert_or_panic(v[110] == 2920);
    assert_or_panic(v[111] == 2921);
    assert_or_panic(v[112] == 2922);
    assert_or_panic(v[113] == 2923);
    assert_or_panic(v[114] == 2924);
    assert_or_panic(v[115] == 2925);
    assert_or_panic(v[116] == 2926);
    assert_or_panic(v[117] == 2927);
    assert_or_panic(v[118] == 2928);
    assert_or_panic(v[119] == 2929);
    assert_or_panic(v[120] == 2930);
    assert_or_panic(v[121] == 2931);
    assert_or_panic(v[122] == 2932);
    assert_or_panic(v[123] == 2933);
    assert_or_panic(v[124] == 2934);
    assert_or_panic(v[125] == 2935);
    assert_or_panic(v[126] == 2936);
    assert_or_panic(v[127] == 2937);
    assert_or_panic(v[128] == 2938);
    assert_or_panic(v[129] == 2939);
    assert_or_panic(v[130] == 2940);
    assert_or_panic(v[131] == 2941);
    assert_or_panic(v[132] == 2942);
    assert_or_panic(v[133] == 2943);
    assert_or_panic(v[134] == 2944);
    assert_or_panic(v[135] == 2945);
    assert_or_panic(v[136] == 2946);
    assert_or_panic(v[137] == 2947);
    assert_or_panic(v[138] == 2948);
    assert_or_panic(v[139] == 2949);
    assert_or_panic(v[140] == 2950);
    assert_or_panic(v[141] == 2951);
    assert_or_panic(v[142] == 2952);
    assert_or_panic(v[143] == 2953);
    assert_or_panic(v[144] == 2954);
    assert_or_panic(v[145] == 2955);
    assert_or_panic(v[146] == 2956);
    assert_or_panic(v[147] == 2957);
    assert_or_panic(v[148] == 2958);
    assert_or_panic(v[149] == 2959);
    assert_or_panic(v[150] == 2960);
    assert_or_panic(v[151] == 2961);
    assert_or_panic(v[152] == 2962);
    assert_or_panic(v[153] == 2963);
    assert_or_panic(v[154] == 2964);
    assert_or_panic(v[155] == 2965);
    assert_or_panic(v[156] == 2966);
    assert_or_panic(v[157] == 2967);
    assert_or_panic(v[158] == 2968);
    assert_or_panic(v[159] == 2969);
    assert_or_panic(v[160] == 2970);
    assert_or_panic(v[161] == 2971);
    assert_or_panic(v[162] == 2972);
    assert_or_panic(v[163] == 2973);
    assert_or_panic(v[164] == 2974);
    assert_or_panic(v[165] == 2975);
    assert_or_panic(v[166] == 2976);
    assert_or_panic(v[167] == 2977);
    assert_or_panic(v[168] == 2978);
    assert_or_panic(v[169] == 2979);
    assert_or_panic(v[170] == 2980);
    assert_or_panic(v[171] == 2981);
    assert_or_panic(v[172] == 2982);
    assert_or_panic(v[173] == 2983);
    assert_or_panic(v[174] == 2984);
    assert_or_panic(v[175] == 2985);
    assert_or_panic(v[176] == 2986);
    assert_or_panic(v[177] == 2987);
    assert_or_panic(v[178] == 2988);
    assert_or_panic(v[179] == 2989);
    assert_or_panic(v[180] == 2990);
    assert_or_panic(v[181] == 2991);
    assert_or_panic(v[182] == 2992);
    assert_or_panic(v[183] == 2993);
    assert_or_panic(v[184] == 2994);
    assert_or_panic(v[185] == 2995);
    assert_or_panic(v[186] == 2996);
    assert_or_panic(v[187] == 2997);
    assert_or_panic(v[188] == 2998);
    assert_or_panic(v[189] == 2999);
    assert_or_panic(v[190] == 3000);
    assert_or_panic(v[191] == 3001);
    assert_or_panic(v[192] == 3002);
    assert_or_panic(v[193] == 3003);
    assert_or_panic(v[194] == 3004);
    assert_or_panic(v[195] == 3005);
    assert_or_panic(v[196] == 3006);
    assert_or_panic(v[197] == 3007);
    assert_or_panic(v[198] == 3008);
    assert_or_panic(v[199] == 3009);
    assert_or_panic(v[200] == 3010);
    assert_or_panic(v[201] == 3011);
    assert_or_panic(v[202] == 3012);
    assert_or_panic(v[203] == 3013);
    assert_or_panic(v[204] == 3014);
    assert_or_panic(v[205] == 3015);
    assert_or_panic(v[206] == 3016);
    assert_or_panic(v[207] == 3017);
    assert_or_panic(v[208] == 3018);
    assert_or_panic(v[209] == 3019);
    assert_or_panic(v[210] == 3020);
    assert_or_panic(v[211] == 3021);
    assert_or_panic(v[212] == 3022);
    assert_or_panic(v[213] == 3023);
    assert_or_panic(v[214] == 3024);
    assert_or_panic(v[215] == 3025);
    assert_or_panic(v[216] == 3026);
    assert_or_panic(v[217] == 3027);
    assert_or_panic(v[218] == 3028);
    assert_or_panic(v[219] == 3029);
    assert_or_panic(v[220] == 3030);
    assert_or_panic(v[221] == 3031);
    assert_or_panic(v[222] == 3032);
    assert_or_panic(v[223] == 3033);
    assert_or_panic(v[224] == 3034);
    assert_or_panic(v[225] == 3035);
    assert_or_panic(v[226] == 3036);
    assert_or_panic(v[227] == 3037);
    assert_or_panic(v[228] == 3038);
    assert_or_panic(v[229] == 3039);
    assert_or_panic(v[230] == 3040);
    assert_or_panic(v[231] == 3041);
    assert_or_panic(v[232] == 3042);
    assert_or_panic(v[233] == 3043);
    assert_or_panic(v[234] == 3044);
    assert_or_panic(v[235] == 3045);
    assert_or_panic(v[236] == 3046);
    assert_or_panic(v[237] == 3047);
    assert_or_panic(v[238] == 3048);
    assert_or_panic(v[239] == 3049);
    assert_or_panic(v[240] == 3050);
    assert_or_panic(v[241] == 3051);
    assert_or_panic(v[242] == 3052);
    assert_or_panic(v[243] == 3053);
    assert_or_panic(v[244] == 3054);
    assert_or_panic(v[245] == 3055);
    assert_or_panic(v[246] == 3056);
    assert_or_panic(v[247] == 3057);
    assert_or_panic(v[248] == 3058);
    assert_or_panic(v[249] == 3059);
    assert_or_panic(v[250] == 3060);
    assert_or_panic(v[251] == 3061);
    assert_or_panic(v[252] == 3062);
    assert_or_panic(v[253] == 3063);
    assert_or_panic(v[254] == 3064);
    assert_or_panic(v[255] == 3065);
    assert_or_panic(i == 256);
}
Vector_256_u16 c_ret_vector_256_u16(void);
void c_vector_256_u16(Vector_256_u16, size_t);
void c_test_vector_256_u16(void);
static void test_vector_256_u16(void) {
    c_abi_current_test = "@Vector(256, u16)";
    Vector_256_u16 v = c_ret_vector_256_u16();
    assert_or_panic(v[0] == 3066);
    assert_or_panic(v[1] == 3067);
    assert_or_panic(v[2] == 3068);
    assert_or_panic(v[3] == 3069);
    assert_or_panic(v[4] == 3070);
    assert_or_panic(v[5] == 3071);
    assert_or_panic(v[6] == 3072);
    assert_or_panic(v[7] == 3073);
    assert_or_panic(v[8] == 3074);
    assert_or_panic(v[9] == 3075);
    assert_or_panic(v[10] == 3076);
    assert_or_panic(v[11] == 3077);
    assert_or_panic(v[12] == 3078);
    assert_or_panic(v[13] == 3079);
    assert_or_panic(v[14] == 3080);
    assert_or_panic(v[15] == 3081);
    assert_or_panic(v[16] == 3082);
    assert_or_panic(v[17] == 3083);
    assert_or_panic(v[18] == 3084);
    assert_or_panic(v[19] == 3085);
    assert_or_panic(v[20] == 3086);
    assert_or_panic(v[21] == 3087);
    assert_or_panic(v[22] == 3088);
    assert_or_panic(v[23] == 3089);
    assert_or_panic(v[24] == 3090);
    assert_or_panic(v[25] == 3091);
    assert_or_panic(v[26] == 3092);
    assert_or_panic(v[27] == 3093);
    assert_or_panic(v[28] == 3094);
    assert_or_panic(v[29] == 3095);
    assert_or_panic(v[30] == 3096);
    assert_or_panic(v[31] == 3097);
    assert_or_panic(v[32] == 3098);
    assert_or_panic(v[33] == 3099);
    assert_or_panic(v[34] == 3100);
    assert_or_panic(v[35] == 3101);
    assert_or_panic(v[36] == 3102);
    assert_or_panic(v[37] == 3103);
    assert_or_panic(v[38] == 3104);
    assert_or_panic(v[39] == 3105);
    assert_or_panic(v[40] == 3106);
    assert_or_panic(v[41] == 3107);
    assert_or_panic(v[42] == 3108);
    assert_or_panic(v[43] == 3109);
    assert_or_panic(v[44] == 3110);
    assert_or_panic(v[45] == 3111);
    assert_or_panic(v[46] == 3112);
    assert_or_panic(v[47] == 3113);
    assert_or_panic(v[48] == 3114);
    assert_or_panic(v[49] == 3115);
    assert_or_panic(v[50] == 3116);
    assert_or_panic(v[51] == 3117);
    assert_or_panic(v[52] == 3118);
    assert_or_panic(v[53] == 3119);
    assert_or_panic(v[54] == 3120);
    assert_or_panic(v[55] == 3121);
    assert_or_panic(v[56] == 3122);
    assert_or_panic(v[57] == 3123);
    assert_or_panic(v[58] == 3124);
    assert_or_panic(v[59] == 3125);
    assert_or_panic(v[60] == 3126);
    assert_or_panic(v[61] == 3127);
    assert_or_panic(v[62] == 3128);
    assert_or_panic(v[63] == 3129);
    assert_or_panic(v[64] == 3130);
    assert_or_panic(v[65] == 3131);
    assert_or_panic(v[66] == 3132);
    assert_or_panic(v[67] == 3133);
    assert_or_panic(v[68] == 3134);
    assert_or_panic(v[69] == 3135);
    assert_or_panic(v[70] == 3136);
    assert_or_panic(v[71] == 3137);
    assert_or_panic(v[72] == 3138);
    assert_or_panic(v[73] == 3139);
    assert_or_panic(v[74] == 3140);
    assert_or_panic(v[75] == 3141);
    assert_or_panic(v[76] == 3142);
    assert_or_panic(v[77] == 3143);
    assert_or_panic(v[78] == 3144);
    assert_or_panic(v[79] == 3145);
    assert_or_panic(v[80] == 3146);
    assert_or_panic(v[81] == 3147);
    assert_or_panic(v[82] == 3148);
    assert_or_panic(v[83] == 3149);
    assert_or_panic(v[84] == 3150);
    assert_or_panic(v[85] == 3151);
    assert_or_panic(v[86] == 3152);
    assert_or_panic(v[87] == 3153);
    assert_or_panic(v[88] == 3154);
    assert_or_panic(v[89] == 3155);
    assert_or_panic(v[90] == 3156);
    assert_or_panic(v[91] == 3157);
    assert_or_panic(v[92] == 3158);
    assert_or_panic(v[93] == 3159);
    assert_or_panic(v[94] == 3160);
    assert_or_panic(v[95] == 3161);
    assert_or_panic(v[96] == 3162);
    assert_or_panic(v[97] == 3163);
    assert_or_panic(v[98] == 3164);
    assert_or_panic(v[99] == 3165);
    assert_or_panic(v[100] == 3166);
    assert_or_panic(v[101] == 3167);
    assert_or_panic(v[102] == 3168);
    assert_or_panic(v[103] == 3169);
    assert_or_panic(v[104] == 3170);
    assert_or_panic(v[105] == 3171);
    assert_or_panic(v[106] == 3172);
    assert_or_panic(v[107] == 3173);
    assert_or_panic(v[108] == 3174);
    assert_or_panic(v[109] == 3175);
    assert_or_panic(v[110] == 3176);
    assert_or_panic(v[111] == 3177);
    assert_or_panic(v[112] == 3178);
    assert_or_panic(v[113] == 3179);
    assert_or_panic(v[114] == 3180);
    assert_or_panic(v[115] == 3181);
    assert_or_panic(v[116] == 3182);
    assert_or_panic(v[117] == 3183);
    assert_or_panic(v[118] == 3184);
    assert_or_panic(v[119] == 3185);
    assert_or_panic(v[120] == 3186);
    assert_or_panic(v[121] == 3187);
    assert_or_panic(v[122] == 3188);
    assert_or_panic(v[123] == 3189);
    assert_or_panic(v[124] == 3190);
    assert_or_panic(v[125] == 3191);
    assert_or_panic(v[126] == 3192);
    assert_or_panic(v[127] == 3193);
    assert_or_panic(v[128] == 3194);
    assert_or_panic(v[129] == 3195);
    assert_or_panic(v[130] == 3196);
    assert_or_panic(v[131] == 3197);
    assert_or_panic(v[132] == 3198);
    assert_or_panic(v[133] == 3199);
    assert_or_panic(v[134] == 3200);
    assert_or_panic(v[135] == 3201);
    assert_or_panic(v[136] == 3202);
    assert_or_panic(v[137] == 3203);
    assert_or_panic(v[138] == 3204);
    assert_or_panic(v[139] == 3205);
    assert_or_panic(v[140] == 3206);
    assert_or_panic(v[141] == 3207);
    assert_or_panic(v[142] == 3208);
    assert_or_panic(v[143] == 3209);
    assert_or_panic(v[144] == 3210);
    assert_or_panic(v[145] == 3211);
    assert_or_panic(v[146] == 3212);
    assert_or_panic(v[147] == 3213);
    assert_or_panic(v[148] == 3214);
    assert_or_panic(v[149] == 3215);
    assert_or_panic(v[150] == 3216);
    assert_or_panic(v[151] == 3217);
    assert_or_panic(v[152] == 3218);
    assert_or_panic(v[153] == 3219);
    assert_or_panic(v[154] == 3220);
    assert_or_panic(v[155] == 3221);
    assert_or_panic(v[156] == 3222);
    assert_or_panic(v[157] == 3223);
    assert_or_panic(v[158] == 3224);
    assert_or_panic(v[159] == 3225);
    assert_or_panic(v[160] == 3226);
    assert_or_panic(v[161] == 3227);
    assert_or_panic(v[162] == 3228);
    assert_or_panic(v[163] == 3229);
    assert_or_panic(v[164] == 3230);
    assert_or_panic(v[165] == 3231);
    assert_or_panic(v[166] == 3232);
    assert_or_panic(v[167] == 3233);
    assert_or_panic(v[168] == 3234);
    assert_or_panic(v[169] == 3235);
    assert_or_panic(v[170] == 3236);
    assert_or_panic(v[171] == 3237);
    assert_or_panic(v[172] == 3238);
    assert_or_panic(v[173] == 3239);
    assert_or_panic(v[174] == 3240);
    assert_or_panic(v[175] == 3241);
    assert_or_panic(v[176] == 3242);
    assert_or_panic(v[177] == 3243);
    assert_or_panic(v[178] == 3244);
    assert_or_panic(v[179] == 3245);
    assert_or_panic(v[180] == 3246);
    assert_or_panic(v[181] == 3247);
    assert_or_panic(v[182] == 3248);
    assert_or_panic(v[183] == 3249);
    assert_or_panic(v[184] == 3250);
    assert_or_panic(v[185] == 3251);
    assert_or_panic(v[186] == 3252);
    assert_or_panic(v[187] == 3253);
    assert_or_panic(v[188] == 3254);
    assert_or_panic(v[189] == 3255);
    assert_or_panic(v[190] == 3256);
    assert_or_panic(v[191] == 3257);
    assert_or_panic(v[192] == 3258);
    assert_or_panic(v[193] == 3259);
    assert_or_panic(v[194] == 3260);
    assert_or_panic(v[195] == 3261);
    assert_or_panic(v[196] == 3262);
    assert_or_panic(v[197] == 3263);
    assert_or_panic(v[198] == 3264);
    assert_or_panic(v[199] == 3265);
    assert_or_panic(v[200] == 3266);
    assert_or_panic(v[201] == 3267);
    assert_or_panic(v[202] == 3268);
    assert_or_panic(v[203] == 3269);
    assert_or_panic(v[204] == 3270);
    assert_or_panic(v[205] == 3271);
    assert_or_panic(v[206] == 3272);
    assert_or_panic(v[207] == 3273);
    assert_or_panic(v[208] == 3274);
    assert_or_panic(v[209] == 3275);
    assert_or_panic(v[210] == 3276);
    assert_or_panic(v[211] == 3277);
    assert_or_panic(v[212] == 3278);
    assert_or_panic(v[213] == 3279);
    assert_or_panic(v[214] == 3280);
    assert_or_panic(v[215] == 3281);
    assert_or_panic(v[216] == 3282);
    assert_or_panic(v[217] == 3283);
    assert_or_panic(v[218] == 3284);
    assert_or_panic(v[219] == 3285);
    assert_or_panic(v[220] == 3286);
    assert_or_panic(v[221] == 3287);
    assert_or_panic(v[222] == 3288);
    assert_or_panic(v[223] == 3289);
    assert_or_panic(v[224] == 3290);
    assert_or_panic(v[225] == 3291);
    assert_or_panic(v[226] == 3292);
    assert_or_panic(v[227] == 3293);
    assert_or_panic(v[228] == 3294);
    assert_or_panic(v[229] == 3295);
    assert_or_panic(v[230] == 3296);
    assert_or_panic(v[231] == 3297);
    assert_or_panic(v[232] == 3298);
    assert_or_panic(v[233] == 3299);
    assert_or_panic(v[234] == 3300);
    assert_or_panic(v[235] == 3301);
    assert_or_panic(v[236] == 3302);
    assert_or_panic(v[237] == 3303);
    assert_or_panic(v[238] == 3304);
    assert_or_panic(v[239] == 3305);
    assert_or_panic(v[240] == 3306);
    assert_or_panic(v[241] == 3307);
    assert_or_panic(v[242] == 3308);
    assert_or_panic(v[243] == 3309);
    assert_or_panic(v[244] == 3310);
    assert_or_panic(v[245] == 3311);
    assert_or_panic(v[246] == 3312);
    assert_or_panic(v[247] == 3313);
    assert_or_panic(v[248] == 3314);
    assert_or_panic(v[249] == 3315);
    assert_or_panic(v[250] == 3316);
    assert_or_panic(v[251] == 3317);
    assert_or_panic(v[252] == 3318);
    assert_or_panic(v[253] == 3319);
    assert_or_panic(v[254] == 3320);
    assert_or_panic(v[255] == 3321);
    c_vector_256_u16((Vector_256_u16){
        3322, 3323, 3324, 3325, 3326, 3327, 3328, 3329, 3330, 3331, 3332, 3333, 3334, 3335, 3336, 3337,
        3338, 3339, 3340, 3341, 3342, 3343, 3344, 3345, 3346, 3347, 3348, 3349, 3350, 3351, 3352, 3353,
        3354, 3355, 3356, 3357, 3358, 3359, 3360, 3361, 3362, 3363, 3364, 3365, 3366, 3367, 3368, 3369,
        3370, 3371, 3372, 3373, 3374, 3375, 3376, 3377, 3378, 3379, 3380, 3381, 3382, 3383, 3384, 3385,
        3386, 3387, 3388, 3389, 3390, 3391, 3392, 3393, 3394, 3395, 3396, 3397, 3398, 3399, 3400, 3401,
        3402, 3403, 3404, 3405, 3406, 3407, 3408, 3409, 3410, 3411, 3412, 3413, 3414, 3415, 3416, 3417,
        3418, 3419, 3420, 3421, 3422, 3423, 3424, 3425, 3426, 3427, 3428, 3429, 3430, 3431, 3432, 3433,
        3434, 3435, 3436, 3437, 3438, 3439, 3440, 3441, 3442, 3443, 3444, 3445, 3446, 3447, 3448, 3449,
        3450, 3451, 3452, 3453, 3454, 3455, 3456, 3457, 3458, 3459, 3460, 3461, 3462, 3463, 3464, 3465,
        3466, 3467, 3468, 3469, 3470, 3471, 3472, 3473, 3474, 3475, 3476, 3477, 3478, 3479, 3480, 3481,
        3482, 3483, 3484, 3485, 3486, 3487, 3488, 3489, 3490, 3491, 3492, 3493, 3494, 3495, 3496, 3497,
        3498, 3499, 3500, 3501, 3502, 3503, 3504, 3505, 3506, 3507, 3508, 3509, 3510, 3511, 3512, 3513,
        3514, 3515, 3516, 3517, 3518, 3519, 3520, 3521, 3522, 3523, 3524, 3525, 3526, 3527, 3528, 3529,
        3530, 3531, 3532, 3533, 3534, 3535, 3536, 3537, 3538, 3539, 3540, 3541, 3542, 3543, 3544, 3545,
        3546, 3547, 3548, 3549, 3550, 3551, 3552, 3553, 3554, 3555, 3556, 3557, 3558, 3559, 3560, 3561,
        3562, 3563, 3564, 3565, 3566, 3567, 3568, 3569, 3570, 3571, 3572, 3573, 3574, 3575, 3576, 3577,
    }, 256);
    c_test_vector_256_u16();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint32_t Vector_1_u32 __attribute__((vector_size(1 * sizeof(uint32_t))));
Vector_1_u32 zig_ret_vector_1_u32(void) {
    return (Vector_1_u32){1};
}
void zig_vector_1_u32(Vector_1_u32 v, size_t i) {
    assert_or_panic(v[0] == 2);
    assert_or_panic(i == 1);
}
Vector_1_u32 c_ret_vector_1_u32(void);
void c_vector_1_u32(Vector_1_u32, size_t);
void c_test_vector_1_u32(void);
static void test_vector_1_u32(void) {
    c_abi_current_test = "@Vector(1, u32)";
#if !(defined(__aarch64__))
    Vector_1_u32 v = c_ret_vector_1_u32();
    assert_or_panic(v[0] == 3);
    c_vector_1_u32((Vector_1_u32){4}, 1);
    c_test_vector_1_u32();
#endif
}
typedef uint32_t Vector_2_u32 __attribute__((vector_size(2 * sizeof(uint32_t))));
Vector_2_u32 zig_ret_vector_2_u32(void) {
    return (Vector_2_u32){ 5, 6 };
}
void zig_vector_2_u32(Vector_2_u32 v, size_t i) {
    assert_or_panic(v[0] == 7);
    assert_or_panic(v[1] == 8);
    assert_or_panic(i == 2);
}
Vector_2_u32 c_ret_vector_2_u32(void);
void c_vector_2_u32(Vector_2_u32, size_t);
void c_test_vector_2_u32(void);
static void test_vector_2_u32(void) {
    c_abi_current_test = "@Vector(2, u32)";
    Vector_2_u32 v = c_ret_vector_2_u32();
    assert_or_panic(v[0] == 9);
    assert_or_panic(v[1] == 10);
    c_vector_2_u32((Vector_2_u32){ 11, 12 }, 2);
    c_test_vector_2_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint32_t Vector_3_u32 __attribute__((vector_size(3 * sizeof(uint32_t))));
Vector_3_u32 zig_ret_vector_3_u32(void) {
    return (Vector_3_u32){ 13, 14, 15 };
}
void zig_vector_3_u32(Vector_3_u32 v, size_t i) {
    assert_or_panic(v[0] == 16);
    assert_or_panic(v[1] == 17);
    assert_or_panic(v[2] == 18);
    assert_or_panic(i == 3);
}
Vector_3_u32 c_ret_vector_3_u32(void);
void c_vector_3_u32(Vector_3_u32, size_t);
void c_test_vector_3_u32(void);
static void test_vector_3_u32(void) {
    c_abi_current_test = "@Vector(3, u32)";
    Vector_3_u32 v = c_ret_vector_3_u32();
    assert_or_panic(v[0] == 19);
    assert_or_panic(v[1] == 20);
    assert_or_panic(v[2] == 21);
    c_vector_3_u32((Vector_3_u32){ 22, 23, 24 }, 3);
    c_test_vector_3_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint32_t Vector_4_u32 __attribute__((vector_size(4 * sizeof(uint32_t))));
Vector_4_u32 zig_ret_vector_4_u32(void) {
    return (Vector_4_u32){ 25, 26, 27, 28 };
}
void zig_vector_4_u32(Vector_4_u32 v, size_t i) {
    assert_or_panic(v[0] == 29);
    assert_or_panic(v[1] == 30);
    assert_or_panic(v[2] == 31);
    assert_or_panic(v[3] == 32);
    assert_or_panic(i == 4);
}
void zig_vector_4_u32_vector_4_u32(Vector_4_u32 v0, Vector_4_u32 v1, size_t i) {
    assert_or_panic(v0[0] == 33);
    assert_or_panic(v0[1] == 34);
    assert_or_panic(v0[2] == 35);
    assert_or_panic(v0[3] == 36);
    assert_or_panic(v1[0] == 37);
    assert_or_panic(v1[1] == 38);
    assert_or_panic(v1[2] == 39);
    assert_or_panic(v1[3] == 40);
    assert_or_panic(i == 8);
}
Vector_4_u32 c_ret_vector_4_u32(void);
void c_vector_4_u32(Vector_4_u32, size_t);
void c_vector_4_u32_vector_4_u32(Vector_4_u32, Vector_4_u32, size_t);
void c_test_vector_4_u32(void);
static void test_vector_4_u32(void) {
    c_abi_current_test = "@Vector(4, u32)";
    Vector_4_u32 v = c_ret_vector_4_u32();
    assert_or_panic(v[0] == 41);
    assert_or_panic(v[1] == 42);
    assert_or_panic(v[2] == 43);
    assert_or_panic(v[3] == 44);
    c_vector_4_u32((Vector_4_u32){ 45, 46, 47, 48 }, 4);
    c_vector_4_u32_vector_4_u32((Vector_4_u32){ 49, 50, 51, 52 }, (Vector_4_u32){ 53, 54, 55, 56 }, 8);
    c_test_vector_4_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint32_t Vector_6_u32 __attribute__((vector_size(6 * sizeof(uint32_t))));
Vector_6_u32 zig_ret_vector_6_u32(void) {
    return (Vector_6_u32){ 41, 42, 43, 44, 45, 46 };
}
void zig_vector_6_u32(Vector_6_u32 v, size_t i) {
    assert_or_panic(v[0] == 47);
    assert_or_panic(v[1] == 48);
    assert_or_panic(v[2] == 49);
    assert_or_panic(v[3] == 50);
    assert_or_panic(v[4] == 51);
    assert_or_panic(v[5] == 52);
    assert_or_panic(i == 6);
}
Vector_6_u32 c_ret_vector_6_u32(void);
void c_vector_6_u32(Vector_6_u32, size_t);
void c_test_vector_6_u32(void);
static void test_vector_6_u32(void) {
    c_abi_current_test = "@Vector(6, u32)";
    Vector_6_u32 v = c_ret_vector_6_u32();
    assert_or_panic(v[0] == 53);
    assert_or_panic(v[1] == 54);
    assert_or_panic(v[2] == 55);
    assert_or_panic(v[3] == 56);
    assert_or_panic(v[4] == 57);
    assert_or_panic(v[5] == 58);
    c_vector_6_u32((Vector_6_u32){ 59, 60, 61, 62, 63, 64 }, 6);
    c_test_vector_6_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint32_t Vector_8_u32 __attribute__((vector_size(8 * sizeof(uint32_t))));
Vector_8_u32 zig_ret_vector_8_u32(void) {
    return (Vector_8_u32){ 65, 66, 67, 68, 69, 70, 71, 72 };
}
void zig_vector_8_u32(Vector_8_u32 v, size_t i) {
    assert_or_panic(v[0] == 73);
    assert_or_panic(v[1] == 74);
    assert_or_panic(v[2] == 75);
    assert_or_panic(v[3] == 76);
    assert_or_panic(v[4] == 77);
    assert_or_panic(v[5] == 78);
    assert_or_panic(v[6] == 79);
    assert_or_panic(v[7] == 80);
    assert_or_panic(i == 8);
}
Vector_8_u32 c_ret_vector_8_u32(void);
void c_vector_8_u32(Vector_8_u32, size_t);
void c_test_vector_8_u32(void);
static void test_vector_8_u32(void) {
    c_abi_current_test = "@Vector(8, u32)";
    Vector_8_u32 v = c_ret_vector_8_u32();
    assert_or_panic(v[0] == 81);
    assert_or_panic(v[1] == 82);
    assert_or_panic(v[2] == 83);
    assert_or_panic(v[3] == 84);
    assert_or_panic(v[4] == 85);
    assert_or_panic(v[5] == 86);
    assert_or_panic(v[6] == 87);
    assert_or_panic(v[7] == 88);
    c_vector_8_u32((Vector_8_u32){ 89, 90, 91, 92, 93, 94, 95, 96 }, 8);
    c_test_vector_8_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint32_t Vector_12_u32 __attribute__((vector_size(12 * sizeof(uint32_t))));
Vector_12_u32 zig_ret_vector_12_u32(void) {
    return (Vector_12_u32){ 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108 };
}
void zig_vector_12_u32(Vector_12_u32 v, size_t i) {
    assert_or_panic(v[0] == 109);
    assert_or_panic(v[1] == 110);
    assert_or_panic(v[2] == 111);
    assert_or_panic(v[3] == 112);
    assert_or_panic(v[4] == 113);
    assert_or_panic(v[5] == 114);
    assert_or_panic(v[6] == 115);
    assert_or_panic(v[7] == 116);
    assert_or_panic(v[8] == 117);
    assert_or_panic(v[9] == 118);
    assert_or_panic(v[10] == 119);
    assert_or_panic(v[11] == 120);
    assert_or_panic(i == 12);
}
Vector_12_u32 c_ret_vector_12_u32(void);
void c_vector_12_u32(Vector_12_u32, size_t);
void c_test_vector_12_u32(void);
static void test_vector_12_u32(void) {
    c_abi_current_test = "@Vector(12, u32)";
    Vector_12_u32 v = c_ret_vector_12_u32();
    assert_or_panic(v[0] == 121);
    assert_or_panic(v[1] == 122);
    assert_or_panic(v[2] == 123);
    assert_or_panic(v[3] == 124);
    assert_or_panic(v[4] == 125);
    assert_or_panic(v[5] == 126);
    assert_or_panic(v[6] == 127);
    assert_or_panic(v[7] == 128);
    assert_or_panic(v[8] == 129);
    assert_or_panic(v[9] == 130);
    assert_or_panic(v[10] == 131);
    assert_or_panic(v[11] == 132);
    c_vector_12_u32((Vector_12_u32){ 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144 }, 12);
    c_test_vector_12_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint32_t Vector_16_u32 __attribute__((vector_size(16 * sizeof(uint32_t))));
Vector_16_u32 zig_ret_vector_16_u32(void) {
    return (Vector_16_u32){ 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160 };
}
void zig_vector_16_u32(Vector_16_u32 v, size_t i) {
    assert_or_panic(v[0] == 161);
    assert_or_panic(v[1] == 162);
    assert_or_panic(v[2] == 163);
    assert_or_panic(v[3] == 164);
    assert_or_panic(v[4] == 165);
    assert_or_panic(v[5] == 166);
    assert_or_panic(v[6] == 167);
    assert_or_panic(v[7] == 168);
    assert_or_panic(v[8] == 169);
    assert_or_panic(v[9] == 170);
    assert_or_panic(v[10] == 171);
    assert_or_panic(v[11] == 172);
    assert_or_panic(v[12] == 173);
    assert_or_panic(v[13] == 174);
    assert_or_panic(v[14] == 175);
    assert_or_panic(v[15] == 176);
    assert_or_panic(i == 16);
}
Vector_16_u32 c_ret_vector_16_u32(void);
void c_vector_16_u32(Vector_16_u32, size_t);
void c_test_vector_16_u32(void);
static void test_vector_16_u32(void) {
    c_abi_current_test = "@Vector(16, u32)";
    Vector_16_u32 v = c_ret_vector_16_u32();
    assert_or_panic(v[0] == 177);
    assert_or_panic(v[1] == 178);
    assert_or_panic(v[2] == 179);
    assert_or_panic(v[3] == 180);
    assert_or_panic(v[4] == 181);
    assert_or_panic(v[5] == 182);
    assert_or_panic(v[6] == 183);
    assert_or_panic(v[7] == 184);
    assert_or_panic(v[8] == 185);
    assert_or_panic(v[9] == 186);
    assert_or_panic(v[10] == 187);
    assert_or_panic(v[11] == 188);
    assert_or_panic(v[12] == 189);
    assert_or_panic(v[13] == 190);
    assert_or_panic(v[14] == 191);
    assert_or_panic(v[15] == 192);
    c_vector_16_u32((Vector_16_u32){ 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208 }, 16);
    c_test_vector_16_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint32_t Vector_24_u32 __attribute__((vector_size(24 * sizeof(uint32_t))));
Vector_24_u32 zig_ret_vector_24_u32(void) {
    return (Vector_24_u32){
    209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232,
    };
}
void zig_vector_24_u32(Vector_24_u32 v, size_t i) {
    assert_or_panic(v[0] == 233);
    assert_or_panic(v[1] == 234);
    assert_or_panic(v[2] == 235);
    assert_or_panic(v[3] == 236);
    assert_or_panic(v[4] == 237);
    assert_or_panic(v[5] == 238);
    assert_or_panic(v[6] == 239);
    assert_or_panic(v[7] == 240);
    assert_or_panic(v[8] == 241);
    assert_or_panic(v[9] == 242);
    assert_or_panic(v[10] == 243);
    assert_or_panic(v[11] == 244);
    assert_or_panic(v[12] == 245);
    assert_or_panic(v[13] == 246);
    assert_or_panic(v[14] == 247);
    assert_or_panic(v[15] == 248);
    assert_or_panic(v[16] == 249);
    assert_or_panic(v[17] == 250);
    assert_or_panic(v[18] == 251);
    assert_or_panic(v[19] == 252);
    assert_or_panic(v[20] == 253);
    assert_or_panic(v[21] == 254);
    assert_or_panic(v[22] == 255);
    assert_or_panic(v[23] == 256);
    assert_or_panic(i == 24);
}
Vector_24_u32 c_ret_vector_24_u32(void);
void c_vector_24_u32(Vector_24_u32, size_t);
void c_test_vector_24_u32(void);
static void test_vector_24_u32(void) {
    c_abi_current_test = "@Vector(24, u32)";
    Vector_24_u32 v = c_ret_vector_24_u32();
    assert_or_panic(v[0] == 257);
    assert_or_panic(v[1] == 258);
    assert_or_panic(v[2] == 259);
    assert_or_panic(v[3] == 260);
    assert_or_panic(v[4] == 261);
    assert_or_panic(v[5] == 262);
    assert_or_panic(v[6] == 263);
    assert_or_panic(v[7] == 264);
    assert_or_panic(v[8] == 265);
    assert_or_panic(v[9] == 266);
    assert_or_panic(v[10] == 267);
    assert_or_panic(v[11] == 268);
    assert_or_panic(v[12] == 269);
    assert_or_panic(v[13] == 270);
    assert_or_panic(v[14] == 271);
    assert_or_panic(v[15] == 272);
    assert_or_panic(v[16] == 273);
    assert_or_panic(v[17] == 274);
    assert_or_panic(v[18] == 275);
    assert_or_panic(v[19] == 276);
    assert_or_panic(v[20] == 277);
    assert_or_panic(v[21] == 278);
    assert_or_panic(v[22] == 279);
    assert_or_panic(v[23] == 280);
    c_vector_24_u32((Vector_24_u32){
        281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296,
        297, 298, 299, 300, 301, 302, 303, 304,
    }, 24);
    c_test_vector_24_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint32_t Vector_32_u32 __attribute__((vector_size(32 * sizeof(uint32_t))));
Vector_32_u32 zig_ret_vector_32_u32(void) {
    return (Vector_32_u32){
    305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320,
    321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336,
    };
}
void zig_vector_32_u32(Vector_32_u32 v, size_t i) {
    assert_or_panic(v[0] == 337);
    assert_or_panic(v[1] == 338);
    assert_or_panic(v[2] == 339);
    assert_or_panic(v[3] == 340);
    assert_or_panic(v[4] == 341);
    assert_or_panic(v[5] == 342);
    assert_or_panic(v[6] == 343);
    assert_or_panic(v[7] == 344);
    assert_or_panic(v[8] == 345);
    assert_or_panic(v[9] == 346);
    assert_or_panic(v[10] == 347);
    assert_or_panic(v[11] == 348);
    assert_or_panic(v[12] == 349);
    assert_or_panic(v[13] == 350);
    assert_or_panic(v[14] == 351);
    assert_or_panic(v[15] == 352);
    assert_or_panic(v[16] == 353);
    assert_or_panic(v[17] == 354);
    assert_or_panic(v[18] == 355);
    assert_or_panic(v[19] == 356);
    assert_or_panic(v[20] == 357);
    assert_or_panic(v[21] == 358);
    assert_or_panic(v[22] == 359);
    assert_or_panic(v[23] == 360);
    assert_or_panic(v[24] == 361);
    assert_or_panic(v[25] == 362);
    assert_or_panic(v[26] == 363);
    assert_or_panic(v[27] == 364);
    assert_or_panic(v[28] == 365);
    assert_or_panic(v[29] == 366);
    assert_or_panic(v[30] == 367);
    assert_or_panic(v[31] == 368);
    assert_or_panic(i == 32);
}
Vector_32_u32 c_ret_vector_32_u32(void);
void c_vector_32_u32(Vector_32_u32, size_t);
void c_test_vector_32_u32(void);
static void test_vector_32_u32(void) {
    c_abi_current_test = "@Vector(32, u32)";
    Vector_32_u32 v = c_ret_vector_32_u32();
    assert_or_panic(v[0] == 369);
    assert_or_panic(v[1] == 370);
    assert_or_panic(v[2] == 371);
    assert_or_panic(v[3] == 372);
    assert_or_panic(v[4] == 373);
    assert_or_panic(v[5] == 374);
    assert_or_panic(v[6] == 375);
    assert_or_panic(v[7] == 376);
    assert_or_panic(v[8] == 377);
    assert_or_panic(v[9] == 378);
    assert_or_panic(v[10] == 379);
    assert_or_panic(v[11] == 380);
    assert_or_panic(v[12] == 381);
    assert_or_panic(v[13] == 382);
    assert_or_panic(v[14] == 383);
    assert_or_panic(v[15] == 384);
    assert_or_panic(v[16] == 385);
    assert_or_panic(v[17] == 386);
    assert_or_panic(v[18] == 387);
    assert_or_panic(v[19] == 388);
    assert_or_panic(v[20] == 389);
    assert_or_panic(v[21] == 390);
    assert_or_panic(v[22] == 391);
    assert_or_panic(v[23] == 392);
    assert_or_panic(v[24] == 393);
    assert_or_panic(v[25] == 394);
    assert_or_panic(v[26] == 395);
    assert_or_panic(v[27] == 396);
    assert_or_panic(v[28] == 397);
    assert_or_panic(v[29] == 398);
    assert_or_panic(v[30] == 399);
    assert_or_panic(v[31] == 400);
    c_vector_32_u32((Vector_32_u32){
        401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416,
        417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432,
    }, 32);
    c_test_vector_32_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint32_t Vector_48_u32 __attribute__((vector_size(48 * sizeof(uint32_t))));
Vector_48_u32 zig_ret_vector_48_u32(void) {
    return (Vector_48_u32){
    433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448,
    449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464,
    465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    };
}
void zig_vector_48_u32(Vector_48_u32 v, size_t i) {
    assert_or_panic(v[0] == 481);
    assert_or_panic(v[1] == 482);
    assert_or_panic(v[2] == 483);
    assert_or_panic(v[3] == 484);
    assert_or_panic(v[4] == 485);
    assert_or_panic(v[5] == 486);
    assert_or_panic(v[6] == 487);
    assert_or_panic(v[7] == 488);
    assert_or_panic(v[8] == 489);
    assert_or_panic(v[9] == 490);
    assert_or_panic(v[10] == 491);
    assert_or_panic(v[11] == 492);
    assert_or_panic(v[12] == 493);
    assert_or_panic(v[13] == 494);
    assert_or_panic(v[14] == 495);
    assert_or_panic(v[15] == 496);
    assert_or_panic(v[16] == 497);
    assert_or_panic(v[17] == 498);
    assert_or_panic(v[18] == 499);
    assert_or_panic(v[19] == 500);
    assert_or_panic(v[20] == 501);
    assert_or_panic(v[21] == 502);
    assert_or_panic(v[22] == 503);
    assert_or_panic(v[23] == 504);
    assert_or_panic(v[24] == 505);
    assert_or_panic(v[25] == 506);
    assert_or_panic(v[26] == 507);
    assert_or_panic(v[27] == 508);
    assert_or_panic(v[28] == 509);
    assert_or_panic(v[29] == 510);
    assert_or_panic(v[30] == 511);
    assert_or_panic(v[31] == 512);
    assert_or_panic(v[32] == 513);
    assert_or_panic(v[33] == 514);
    assert_or_panic(v[34] == 515);
    assert_or_panic(v[35] == 516);
    assert_or_panic(v[36] == 517);
    assert_or_panic(v[37] == 518);
    assert_or_panic(v[38] == 519);
    assert_or_panic(v[39] == 520);
    assert_or_panic(v[40] == 521);
    assert_or_panic(v[41] == 522);
    assert_or_panic(v[42] == 523);
    assert_or_panic(v[43] == 524);
    assert_or_panic(v[44] == 525);
    assert_or_panic(v[45] == 526);
    assert_or_panic(v[46] == 527);
    assert_or_panic(v[47] == 528);
    assert_or_panic(i == 48);
}
Vector_48_u32 c_ret_vector_48_u32(void);
void c_vector_48_u32(Vector_48_u32, size_t);
void c_test_vector_48_u32(void);
static void test_vector_48_u32(void) {
    c_abi_current_test = "@Vector(48, u32)";
    Vector_48_u32 v = c_ret_vector_48_u32();
    assert_or_panic(v[0] == 529);
    assert_or_panic(v[1] == 530);
    assert_or_panic(v[2] == 531);
    assert_or_panic(v[3] == 532);
    assert_or_panic(v[4] == 533);
    assert_or_panic(v[5] == 534);
    assert_or_panic(v[6] == 535);
    assert_or_panic(v[7] == 536);
    assert_or_panic(v[8] == 537);
    assert_or_panic(v[9] == 538);
    assert_or_panic(v[10] == 539);
    assert_or_panic(v[11] == 540);
    assert_or_panic(v[12] == 541);
    assert_or_panic(v[13] == 542);
    assert_or_panic(v[14] == 543);
    assert_or_panic(v[15] == 544);
    assert_or_panic(v[16] == 545);
    assert_or_panic(v[17] == 546);
    assert_or_panic(v[18] == 547);
    assert_or_panic(v[19] == 548);
    assert_or_panic(v[20] == 549);
    assert_or_panic(v[21] == 550);
    assert_or_panic(v[22] == 551);
    assert_or_panic(v[23] == 552);
    assert_or_panic(v[24] == 553);
    assert_or_panic(v[25] == 554);
    assert_or_panic(v[26] == 555);
    assert_or_panic(v[27] == 556);
    assert_or_panic(v[28] == 557);
    assert_or_panic(v[29] == 558);
    assert_or_panic(v[30] == 559);
    assert_or_panic(v[31] == 560);
    assert_or_panic(v[32] == 561);
    assert_or_panic(v[33] == 562);
    assert_or_panic(v[34] == 563);
    assert_or_panic(v[35] == 564);
    assert_or_panic(v[36] == 565);
    assert_or_panic(v[37] == 566);
    assert_or_panic(v[38] == 567);
    assert_or_panic(v[39] == 568);
    assert_or_panic(v[40] == 569);
    assert_or_panic(v[41] == 570);
    assert_or_panic(v[42] == 571);
    assert_or_panic(v[43] == 572);
    assert_or_panic(v[44] == 573);
    assert_or_panic(v[45] == 574);
    assert_or_panic(v[46] == 575);
    assert_or_panic(v[47] == 576);
    c_vector_48_u32((Vector_48_u32){
        577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592,
        593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608,
        609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624,
    }, 48);
    c_test_vector_48_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint32_t Vector_64_u32 __attribute__((vector_size(64 * sizeof(uint32_t))));
Vector_64_u32 zig_ret_vector_64_u32(void) {
    return (Vector_64_u32){
    625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640,
    641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656,
    657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672,
    673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688,
    };
}
void zig_vector_64_u32(Vector_64_u32 v, size_t i) {
    assert_or_panic(v[0] == 689);
    assert_or_panic(v[1] == 690);
    assert_or_panic(v[2] == 691);
    assert_or_panic(v[3] == 692);
    assert_or_panic(v[4] == 693);
    assert_or_panic(v[5] == 694);
    assert_or_panic(v[6] == 695);
    assert_or_panic(v[7] == 696);
    assert_or_panic(v[8] == 697);
    assert_or_panic(v[9] == 698);
    assert_or_panic(v[10] == 699);
    assert_or_panic(v[11] == 700);
    assert_or_panic(v[12] == 701);
    assert_or_panic(v[13] == 702);
    assert_or_panic(v[14] == 703);
    assert_or_panic(v[15] == 704);
    assert_or_panic(v[16] == 705);
    assert_or_panic(v[17] == 706);
    assert_or_panic(v[18] == 707);
    assert_or_panic(v[19] == 708);
    assert_or_panic(v[20] == 709);
    assert_or_panic(v[21] == 710);
    assert_or_panic(v[22] == 711);
    assert_or_panic(v[23] == 712);
    assert_or_panic(v[24] == 713);
    assert_or_panic(v[25] == 714);
    assert_or_panic(v[26] == 715);
    assert_or_panic(v[27] == 716);
    assert_or_panic(v[28] == 717);
    assert_or_panic(v[29] == 718);
    assert_or_panic(v[30] == 719);
    assert_or_panic(v[31] == 720);
    assert_or_panic(v[32] == 721);
    assert_or_panic(v[33] == 722);
    assert_or_panic(v[34] == 723);
    assert_or_panic(v[35] == 724);
    assert_or_panic(v[36] == 725);
    assert_or_panic(v[37] == 726);
    assert_or_panic(v[38] == 727);
    assert_or_panic(v[39] == 728);
    assert_or_panic(v[40] == 729);
    assert_or_panic(v[41] == 730);
    assert_or_panic(v[42] == 731);
    assert_or_panic(v[43] == 732);
    assert_or_panic(v[44] == 733);
    assert_or_panic(v[45] == 734);
    assert_or_panic(v[46] == 735);
    assert_or_panic(v[47] == 736);
    assert_or_panic(v[48] == 737);
    assert_or_panic(v[49] == 738);
    assert_or_panic(v[50] == 739);
    assert_or_panic(v[51] == 740);
    assert_or_panic(v[52] == 741);
    assert_or_panic(v[53] == 742);
    assert_or_panic(v[54] == 743);
    assert_or_panic(v[55] == 744);
    assert_or_panic(v[56] == 745);
    assert_or_panic(v[57] == 746);
    assert_or_panic(v[58] == 747);
    assert_or_panic(v[59] == 748);
    assert_or_panic(v[60] == 749);
    assert_or_panic(v[61] == 750);
    assert_or_panic(v[62] == 751);
    assert_or_panic(v[63] == 752);
    assert_or_panic(i == 64);
}
Vector_64_u32 c_ret_vector_64_u32(void);
void c_vector_64_u32(Vector_64_u32, size_t);
void c_test_vector_64_u32(void);
static void test_vector_64_u32(void) {
    c_abi_current_test = "@Vector(64, u32)";
    Vector_64_u32 v = c_ret_vector_64_u32();
    assert_or_panic(v[0] == 753);
    assert_or_panic(v[1] == 754);
    assert_or_panic(v[2] == 755);
    assert_or_panic(v[3] == 756);
    assert_or_panic(v[4] == 757);
    assert_or_panic(v[5] == 758);
    assert_or_panic(v[6] == 759);
    assert_or_panic(v[7] == 760);
    assert_or_panic(v[8] == 761);
    assert_or_panic(v[9] == 762);
    assert_or_panic(v[10] == 763);
    assert_or_panic(v[11] == 764);
    assert_or_panic(v[12] == 765);
    assert_or_panic(v[13] == 766);
    assert_or_panic(v[14] == 767);
    assert_or_panic(v[15] == 768);
    assert_or_panic(v[16] == 769);
    assert_or_panic(v[17] == 770);
    assert_or_panic(v[18] == 771);
    assert_or_panic(v[19] == 772);
    assert_or_panic(v[20] == 773);
    assert_or_panic(v[21] == 774);
    assert_or_panic(v[22] == 775);
    assert_or_panic(v[23] == 776);
    assert_or_panic(v[24] == 777);
    assert_or_panic(v[25] == 778);
    assert_or_panic(v[26] == 779);
    assert_or_panic(v[27] == 780);
    assert_or_panic(v[28] == 781);
    assert_or_panic(v[29] == 782);
    assert_or_panic(v[30] == 783);
    assert_or_panic(v[31] == 784);
    assert_or_panic(v[32] == 785);
    assert_or_panic(v[33] == 786);
    assert_or_panic(v[34] == 787);
    assert_or_panic(v[35] == 788);
    assert_or_panic(v[36] == 789);
    assert_or_panic(v[37] == 790);
    assert_or_panic(v[38] == 791);
    assert_or_panic(v[39] == 792);
    assert_or_panic(v[40] == 793);
    assert_or_panic(v[41] == 794);
    assert_or_panic(v[42] == 795);
    assert_or_panic(v[43] == 796);
    assert_or_panic(v[44] == 797);
    assert_or_panic(v[45] == 798);
    assert_or_panic(v[46] == 799);
    assert_or_panic(v[47] == 800);
    assert_or_panic(v[48] == 801);
    assert_or_panic(v[49] == 802);
    assert_or_panic(v[50] == 803);
    assert_or_panic(v[51] == 804);
    assert_or_panic(v[52] == 805);
    assert_or_panic(v[53] == 806);
    assert_or_panic(v[54] == 807);
    assert_or_panic(v[55] == 808);
    assert_or_panic(v[56] == 809);
    assert_or_panic(v[57] == 810);
    assert_or_panic(v[58] == 811);
    assert_or_panic(v[59] == 812);
    assert_or_panic(v[60] == 813);
    assert_or_panic(v[61] == 814);
    assert_or_panic(v[62] == 815);
    assert_or_panic(v[63] == 816);
    c_vector_64_u32((Vector_64_u32){
        817, 818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 832,
        833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848,
        849, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864,
        865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880,
    }, 64);
    c_test_vector_64_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint32_t Vector_96_u32 __attribute__((vector_size(96 * sizeof(uint32_t))));
Vector_96_u32 zig_ret_vector_96_u32(void) {
    return (Vector_96_u32){
    890, 891, 892, 893, 894, 895, 896, 897, 898, 899, 900, 901, 902, 903, 904, 905,
    906, 907, 908, 909, 910, 911, 912, 913, 914, 915, 916, 917, 918, 919, 920, 921,
    922, 923, 924, 925, 926, 927, 928, 929, 930, 931, 932, 933, 934, 935, 936, 937,
    938, 939, 940, 941, 942, 943, 944, 945, 946, 947, 948, 949, 950, 951, 952, 953,
    954, 955, 956, 957, 958, 959, 960, 961, 962, 963, 964, 965, 966, 967, 968, 969,
    970, 971, 972, 973, 974, 975, 976, 977, 978, 979, 980, 981, 982, 983, 984, 985,
    };
}
void zig_vector_96_u32(Vector_96_u32 v, size_t i) {
    assert_or_panic(v[0] == 986);
    assert_or_panic(v[1] == 987);
    assert_or_panic(v[2] == 988);
    assert_or_panic(v[3] == 989);
    assert_or_panic(v[4] == 990);
    assert_or_panic(v[5] == 991);
    assert_or_panic(v[6] == 992);
    assert_or_panic(v[7] == 993);
    assert_or_panic(v[8] == 994);
    assert_or_panic(v[9] == 995);
    assert_or_panic(v[10] == 996);
    assert_or_panic(v[11] == 997);
    assert_or_panic(v[12] == 998);
    assert_or_panic(v[13] == 999);
    assert_or_panic(v[14] == 1000);
    assert_or_panic(v[15] == 1001);
    assert_or_panic(v[16] == 1002);
    assert_or_panic(v[17] == 1003);
    assert_or_panic(v[18] == 1004);
    assert_or_panic(v[19] == 1005);
    assert_or_panic(v[20] == 1006);
    assert_or_panic(v[21] == 1007);
    assert_or_panic(v[22] == 1008);
    assert_or_panic(v[23] == 1009);
    assert_or_panic(v[24] == 1010);
    assert_or_panic(v[25] == 1011);
    assert_or_panic(v[26] == 1012);
    assert_or_panic(v[27] == 1013);
    assert_or_panic(v[28] == 1014);
    assert_or_panic(v[29] == 1015);
    assert_or_panic(v[30] == 1016);
    assert_or_panic(v[31] == 1017);
    assert_or_panic(v[32] == 1018);
    assert_or_panic(v[33] == 1019);
    assert_or_panic(v[34] == 1020);
    assert_or_panic(v[35] == 1021);
    assert_or_panic(v[36] == 1022);
    assert_or_panic(v[37] == 1023);
    assert_or_panic(v[38] == 1024);
    assert_or_panic(v[39] == 1025);
    assert_or_panic(v[40] == 1026);
    assert_or_panic(v[41] == 1027);
    assert_or_panic(v[42] == 1028);
    assert_or_panic(v[43] == 1029);
    assert_or_panic(v[44] == 1030);
    assert_or_panic(v[45] == 1031);
    assert_or_panic(v[46] == 1032);
    assert_or_panic(v[47] == 1033);
    assert_or_panic(v[48] == 1034);
    assert_or_panic(v[49] == 1035);
    assert_or_panic(v[50] == 1036);
    assert_or_panic(v[51] == 1037);
    assert_or_panic(v[52] == 1038);
    assert_or_panic(v[53] == 1039);
    assert_or_panic(v[54] == 1040);
    assert_or_panic(v[55] == 1041);
    assert_or_panic(v[56] == 1042);
    assert_or_panic(v[57] == 1043);
    assert_or_panic(v[58] == 1044);
    assert_or_panic(v[59] == 1045);
    assert_or_panic(v[60] == 1046);
    assert_or_panic(v[61] == 1047);
    assert_or_panic(v[62] == 1048);
    assert_or_panic(v[63] == 1049);
    assert_or_panic(v[64] == 1050);
    assert_or_panic(v[65] == 1051);
    assert_or_panic(v[66] == 1052);
    assert_or_panic(v[67] == 1053);
    assert_or_panic(v[68] == 1054);
    assert_or_panic(v[69] == 1055);
    assert_or_panic(v[70] == 1056);
    assert_or_panic(v[71] == 1057);
    assert_or_panic(v[72] == 1058);
    assert_or_panic(v[73] == 1059);
    assert_or_panic(v[74] == 1060);
    assert_or_panic(v[75] == 1061);
    assert_or_panic(v[76] == 1062);
    assert_or_panic(v[77] == 1063);
    assert_or_panic(v[78] == 1064);
    assert_or_panic(v[79] == 1065);
    assert_or_panic(v[80] == 1066);
    assert_or_panic(v[81] == 1067);
    assert_or_panic(v[82] == 1068);
    assert_or_panic(v[83] == 1069);
    assert_or_panic(v[84] == 1070);
    assert_or_panic(v[85] == 1071);
    assert_or_panic(v[86] == 1072);
    assert_or_panic(v[87] == 1073);
    assert_or_panic(v[88] == 1074);
    assert_or_panic(v[89] == 1075);
    assert_or_panic(v[90] == 1076);
    assert_or_panic(v[91] == 1077);
    assert_or_panic(v[92] == 1078);
    assert_or_panic(v[93] == 1079);
    assert_or_panic(v[94] == 1080);
    assert_or_panic(v[95] == 1081);
    assert_or_panic(i == 96);
}
Vector_96_u32 c_ret_vector_96_u32(void);
void c_vector_96_u32(Vector_96_u32, size_t);
void c_test_vector_96_u32(void);
static void test_vector_96_u32(void) {
    c_abi_current_test = "@Vector(96, u32)";
    Vector_96_u32 v = c_ret_vector_96_u32();
    assert_or_panic(v[0] == 1082);
    assert_or_panic(v[1] == 1083);
    assert_or_panic(v[2] == 1084);
    assert_or_panic(v[3] == 1085);
    assert_or_panic(v[4] == 1086);
    assert_or_panic(v[5] == 1087);
    assert_or_panic(v[6] == 1088);
    assert_or_panic(v[7] == 1089);
    assert_or_panic(v[8] == 1090);
    assert_or_panic(v[9] == 1091);
    assert_or_panic(v[10] == 1092);
    assert_or_panic(v[11] == 1093);
    assert_or_panic(v[12] == 1094);
    assert_or_panic(v[13] == 1095);
    assert_or_panic(v[14] == 1096);
    assert_or_panic(v[15] == 1097);
    assert_or_panic(v[16] == 1098);
    assert_or_panic(v[17] == 1099);
    assert_or_panic(v[18] == 1100);
    assert_or_panic(v[19] == 1101);
    assert_or_panic(v[20] == 1102);
    assert_or_panic(v[21] == 1103);
    assert_or_panic(v[22] == 1104);
    assert_or_panic(v[23] == 1105);
    assert_or_panic(v[24] == 1106);
    assert_or_panic(v[25] == 1107);
    assert_or_panic(v[26] == 1108);
    assert_or_panic(v[27] == 1109);
    assert_or_panic(v[28] == 1110);
    assert_or_panic(v[29] == 1111);
    assert_or_panic(v[30] == 1112);
    assert_or_panic(v[31] == 1113);
    assert_or_panic(v[32] == 1114);
    assert_or_panic(v[33] == 1115);
    assert_or_panic(v[34] == 1116);
    assert_or_panic(v[35] == 1117);
    assert_or_panic(v[36] == 1118);
    assert_or_panic(v[37] == 1119);
    assert_or_panic(v[38] == 1120);
    assert_or_panic(v[39] == 1121);
    assert_or_panic(v[40] == 1122);
    assert_or_panic(v[41] == 1123);
    assert_or_panic(v[42] == 1124);
    assert_or_panic(v[43] == 1125);
    assert_or_panic(v[44] == 1126);
    assert_or_panic(v[45] == 1127);
    assert_or_panic(v[46] == 1128);
    assert_or_panic(v[47] == 1129);
    assert_or_panic(v[48] == 1130);
    assert_or_panic(v[49] == 1131);
    assert_or_panic(v[50] == 1132);
    assert_or_panic(v[51] == 1133);
    assert_or_panic(v[52] == 1134);
    assert_or_panic(v[53] == 1135);
    assert_or_panic(v[54] == 1136);
    assert_or_panic(v[55] == 1137);
    assert_or_panic(v[56] == 1138);
    assert_or_panic(v[57] == 1139);
    assert_or_panic(v[58] == 1140);
    assert_or_panic(v[59] == 1141);
    assert_or_panic(v[60] == 1142);
    assert_or_panic(v[61] == 1143);
    assert_or_panic(v[62] == 1144);
    assert_or_panic(v[63] == 1145);
    assert_or_panic(v[64] == 1146);
    assert_or_panic(v[65] == 1147);
    assert_or_panic(v[66] == 1148);
    assert_or_panic(v[67] == 1149);
    assert_or_panic(v[68] == 1150);
    assert_or_panic(v[69] == 1151);
    assert_or_panic(v[70] == 1152);
    assert_or_panic(v[71] == 1153);
    assert_or_panic(v[72] == 1154);
    assert_or_panic(v[73] == 1155);
    assert_or_panic(v[74] == 1156);
    assert_or_panic(v[75] == 1157);
    assert_or_panic(v[76] == 1158);
    assert_or_panic(v[77] == 1159);
    assert_or_panic(v[78] == 1160);
    assert_or_panic(v[79] == 1161);
    assert_or_panic(v[80] == 1162);
    assert_or_panic(v[81] == 1163);
    assert_or_panic(v[82] == 1164);
    assert_or_panic(v[83] == 1165);
    assert_or_panic(v[84] == 1166);
    assert_or_panic(v[85] == 1167);
    assert_or_panic(v[86] == 1168);
    assert_or_panic(v[87] == 1169);
    assert_or_panic(v[88] == 1170);
    assert_or_panic(v[89] == 1171);
    assert_or_panic(v[90] == 1172);
    assert_or_panic(v[91] == 1173);
    assert_or_panic(v[92] == 1174);
    assert_or_panic(v[93] == 1175);
    assert_or_panic(v[94] == 1176);
    assert_or_panic(v[95] == 1177);
    c_vector_96_u32((Vector_96_u32){
        1178, 1179, 1180, 1181, 1182, 1183, 1184, 1185, 1186, 1187, 1188, 1189, 1190, 1191, 1192, 1193,
        1194, 1195, 1196, 1197, 1198, 1199, 1200, 1201, 1202, 1203, 1204, 1205, 1206, 1207, 1208, 1209,
        1210, 1211, 1212, 1213, 1214, 1215, 1216, 1217, 1218, 1219, 1220, 1221, 1222, 1223, 1224, 1225,
        1226, 1227, 1228, 1229, 1230, 1231, 1232, 1233, 1234, 1235, 1236, 1237, 1238, 1239, 1240, 1241,
        1242, 1243, 1244, 1245, 1246, 1247, 1248, 1249, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257,
        1258, 1259, 1260, 1261, 1262, 1263, 1264, 1265, 1266, 1267, 1268, 1269, 1270, 1271, 1272, 1273,
    }, 96);
    c_test_vector_96_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint32_t Vector_128_u32 __attribute__((vector_size(128 * sizeof(uint32_t))));
Vector_128_u32 zig_ret_vector_128_u32(void) {
    return (Vector_128_u32){
    1274, 1275, 1276, 1277, 1278, 1279, 1280, 1281, 1282, 1283, 1284, 1285, 1286, 1287, 1288, 1289,
    1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1298, 1299, 1300, 1301, 1302, 1303, 1304, 1305,
    1306, 1307, 1308, 1309, 1310, 1311, 1312, 1313, 1314, 1315, 1316, 1317, 1318, 1319, 1320, 1321,
    1322, 1323, 1324, 1325, 1326, 1327, 1328, 1329, 1330, 1331, 1332, 1333, 1334, 1335, 1336, 1337,
    1338, 1339, 1340, 1341, 1342, 1343, 1344, 1345, 1346, 1347, 1348, 1349, 1350, 1351, 1352, 1353,
    1354, 1355, 1356, 1357, 1358, 1359, 1360, 1361, 1362, 1363, 1364, 1365, 1366, 1367, 1368, 1369,
    1370, 1371, 1372, 1373, 1374, 1375, 1376, 1377, 1378, 1379, 1380, 1381, 1382, 1383, 1384, 1385,
    1386, 1387, 1388, 1389, 1390, 1391, 1392, 1393, 1394, 1395, 1396, 1397, 1398, 1399, 1400, 1401,
    };
}
void zig_vector_128_u32(Vector_128_u32 v, size_t i) {
    assert_or_panic(v[0] == 1402);
    assert_or_panic(v[1] == 1403);
    assert_or_panic(v[2] == 1404);
    assert_or_panic(v[3] == 1405);
    assert_or_panic(v[4] == 1406);
    assert_or_panic(v[5] == 1407);
    assert_or_panic(v[6] == 1408);
    assert_or_panic(v[7] == 1409);
    assert_or_panic(v[8] == 1410);
    assert_or_panic(v[9] == 1411);
    assert_or_panic(v[10] == 1412);
    assert_or_panic(v[11] == 1413);
    assert_or_panic(v[12] == 1414);
    assert_or_panic(v[13] == 1415);
    assert_or_panic(v[14] == 1416);
    assert_or_panic(v[15] == 1417);
    assert_or_panic(v[16] == 1418);
    assert_or_panic(v[17] == 1419);
    assert_or_panic(v[18] == 1420);
    assert_or_panic(v[19] == 1421);
    assert_or_panic(v[20] == 1422);
    assert_or_panic(v[21] == 1423);
    assert_or_panic(v[22] == 1424);
    assert_or_panic(v[23] == 1425);
    assert_or_panic(v[24] == 1426);
    assert_or_panic(v[25] == 1427);
    assert_or_panic(v[26] == 1428);
    assert_or_panic(v[27] == 1429);
    assert_or_panic(v[28] == 1430);
    assert_or_panic(v[29] == 1431);
    assert_or_panic(v[30] == 1432);
    assert_or_panic(v[31] == 1433);
    assert_or_panic(v[32] == 1434);
    assert_or_panic(v[33] == 1435);
    assert_or_panic(v[34] == 1436);
    assert_or_panic(v[35] == 1437);
    assert_or_panic(v[36] == 1438);
    assert_or_panic(v[37] == 1439);
    assert_or_panic(v[38] == 1440);
    assert_or_panic(v[39] == 1441);
    assert_or_panic(v[40] == 1442);
    assert_or_panic(v[41] == 1443);
    assert_or_panic(v[42] == 1444);
    assert_or_panic(v[43] == 1445);
    assert_or_panic(v[44] == 1446);
    assert_or_panic(v[45] == 1447);
    assert_or_panic(v[46] == 1448);
    assert_or_panic(v[47] == 1449);
    assert_or_panic(v[48] == 1450);
    assert_or_panic(v[49] == 1451);
    assert_or_panic(v[50] == 1452);
    assert_or_panic(v[51] == 1453);
    assert_or_panic(v[52] == 1454);
    assert_or_panic(v[53] == 1455);
    assert_or_panic(v[54] == 1456);
    assert_or_panic(v[55] == 1457);
    assert_or_panic(v[56] == 1458);
    assert_or_panic(v[57] == 1459);
    assert_or_panic(v[58] == 1460);
    assert_or_panic(v[59] == 1461);
    assert_or_panic(v[60] == 1462);
    assert_or_panic(v[61] == 1463);
    assert_or_panic(v[62] == 1464);
    assert_or_panic(v[63] == 1465);
    assert_or_panic(v[64] == 1466);
    assert_or_panic(v[65] == 1467);
    assert_or_panic(v[66] == 1468);
    assert_or_panic(v[67] == 1469);
    assert_or_panic(v[68] == 1470);
    assert_or_panic(v[69] == 1471);
    assert_or_panic(v[70] == 1472);
    assert_or_panic(v[71] == 1473);
    assert_or_panic(v[72] == 1474);
    assert_or_panic(v[73] == 1475);
    assert_or_panic(v[74] == 1476);
    assert_or_panic(v[75] == 1477);
    assert_or_panic(v[76] == 1478);
    assert_or_panic(v[77] == 1479);
    assert_or_panic(v[78] == 1480);
    assert_or_panic(v[79] == 1481);
    assert_or_panic(v[80] == 1482);
    assert_or_panic(v[81] == 1483);
    assert_or_panic(v[82] == 1484);
    assert_or_panic(v[83] == 1485);
    assert_or_panic(v[84] == 1486);
    assert_or_panic(v[85] == 1487);
    assert_or_panic(v[86] == 1488);
    assert_or_panic(v[87] == 1489);
    assert_or_panic(v[88] == 1490);
    assert_or_panic(v[89] == 1491);
    assert_or_panic(v[90] == 1492);
    assert_or_panic(v[91] == 1493);
    assert_or_panic(v[92] == 1494);
    assert_or_panic(v[93] == 1495);
    assert_or_panic(v[94] == 1496);
    assert_or_panic(v[95] == 1497);
    assert_or_panic(v[96] == 1498);
    assert_or_panic(v[97] == 1499);
    assert_or_panic(v[98] == 1500);
    assert_or_panic(v[99] == 1501);
    assert_or_panic(v[100] == 1502);
    assert_or_panic(v[101] == 1503);
    assert_or_panic(v[102] == 1504);
    assert_or_panic(v[103] == 1505);
    assert_or_panic(v[104] == 1506);
    assert_or_panic(v[105] == 1507);
    assert_or_panic(v[106] == 1508);
    assert_or_panic(v[107] == 1509);
    assert_or_panic(v[108] == 1510);
    assert_or_panic(v[109] == 1511);
    assert_or_panic(v[110] == 1512);
    assert_or_panic(v[111] == 1513);
    assert_or_panic(v[112] == 1514);
    assert_or_panic(v[113] == 1515);
    assert_or_panic(v[114] == 1516);
    assert_or_panic(v[115] == 1517);
    assert_or_panic(v[116] == 1518);
    assert_or_panic(v[117] == 1519);
    assert_or_panic(v[118] == 1520);
    assert_or_panic(v[119] == 1521);
    assert_or_panic(v[120] == 1522);
    assert_or_panic(v[121] == 1523);
    assert_or_panic(v[122] == 1524);
    assert_or_panic(v[123] == 1525);
    assert_or_panic(v[124] == 1526);
    assert_or_panic(v[125] == 1527);
    assert_or_panic(v[126] == 1528);
    assert_or_panic(v[127] == 1529);
    assert_or_panic(i == 128);
}
Vector_128_u32 c_ret_vector_128_u32(void);
void c_vector_128_u32(Vector_128_u32, size_t);
void c_test_vector_128_u32(void);
static void test_vector_128_u32(void) {
    c_abi_current_test = "@Vector(128, u32)";
    Vector_128_u32 v = c_ret_vector_128_u32();
    assert_or_panic(v[0] == 1530);
    assert_or_panic(v[1] == 1531);
    assert_or_panic(v[2] == 1532);
    assert_or_panic(v[3] == 1533);
    assert_or_panic(v[4] == 1534);
    assert_or_panic(v[5] == 1535);
    assert_or_panic(v[6] == 1536);
    assert_or_panic(v[7] == 1537);
    assert_or_panic(v[8] == 1538);
    assert_or_panic(v[9] == 1539);
    assert_or_panic(v[10] == 1540);
    assert_or_panic(v[11] == 1541);
    assert_or_panic(v[12] == 1542);
    assert_or_panic(v[13] == 1543);
    assert_or_panic(v[14] == 1544);
    assert_or_panic(v[15] == 1545);
    assert_or_panic(v[16] == 1546);
    assert_or_panic(v[17] == 1547);
    assert_or_panic(v[18] == 1548);
    assert_or_panic(v[19] == 1549);
    assert_or_panic(v[20] == 1550);
    assert_or_panic(v[21] == 1551);
    assert_or_panic(v[22] == 1552);
    assert_or_panic(v[23] == 1553);
    assert_or_panic(v[24] == 1554);
    assert_or_panic(v[25] == 1555);
    assert_or_panic(v[26] == 1556);
    assert_or_panic(v[27] == 1557);
    assert_or_panic(v[28] == 1558);
    assert_or_panic(v[29] == 1559);
    assert_or_panic(v[30] == 1560);
    assert_or_panic(v[31] == 1561);
    assert_or_panic(v[32] == 1562);
    assert_or_panic(v[33] == 1563);
    assert_or_panic(v[34] == 1564);
    assert_or_panic(v[35] == 1565);
    assert_or_panic(v[36] == 1566);
    assert_or_panic(v[37] == 1567);
    assert_or_panic(v[38] == 1568);
    assert_or_panic(v[39] == 1569);
    assert_or_panic(v[40] == 1570);
    assert_or_panic(v[41] == 1571);
    assert_or_panic(v[42] == 1572);
    assert_or_panic(v[43] == 1573);
    assert_or_panic(v[44] == 1574);
    assert_or_panic(v[45] == 1575);
    assert_or_panic(v[46] == 1576);
    assert_or_panic(v[47] == 1577);
    assert_or_panic(v[48] == 1578);
    assert_or_panic(v[49] == 1579);
    assert_or_panic(v[50] == 1580);
    assert_or_panic(v[51] == 1581);
    assert_or_panic(v[52] == 1582);
    assert_or_panic(v[53] == 1583);
    assert_or_panic(v[54] == 1584);
    assert_or_panic(v[55] == 1585);
    assert_or_panic(v[56] == 1586);
    assert_or_panic(v[57] == 1587);
    assert_or_panic(v[58] == 1588);
    assert_or_panic(v[59] == 1589);
    assert_or_panic(v[60] == 1590);
    assert_or_panic(v[61] == 1591);
    assert_or_panic(v[62] == 1592);
    assert_or_panic(v[63] == 1593);
    assert_or_panic(v[64] == 1594);
    assert_or_panic(v[65] == 1595);
    assert_or_panic(v[66] == 1596);
    assert_or_panic(v[67] == 1597);
    assert_or_panic(v[68] == 1598);
    assert_or_panic(v[69] == 1599);
    assert_or_panic(v[70] == 1600);
    assert_or_panic(v[71] == 1601);
    assert_or_panic(v[72] == 1602);
    assert_or_panic(v[73] == 1603);
    assert_or_panic(v[74] == 1604);
    assert_or_panic(v[75] == 1605);
    assert_or_panic(v[76] == 1606);
    assert_or_panic(v[77] == 1607);
    assert_or_panic(v[78] == 1608);
    assert_or_panic(v[79] == 1609);
    assert_or_panic(v[80] == 1610);
    assert_or_panic(v[81] == 1611);
    assert_or_panic(v[82] == 1612);
    assert_or_panic(v[83] == 1613);
    assert_or_panic(v[84] == 1614);
    assert_or_panic(v[85] == 1615);
    assert_or_panic(v[86] == 1616);
    assert_or_panic(v[87] == 1617);
    assert_or_panic(v[88] == 1618);
    assert_or_panic(v[89] == 1619);
    assert_or_panic(v[90] == 1620);
    assert_or_panic(v[91] == 1621);
    assert_or_panic(v[92] == 1622);
    assert_or_panic(v[93] == 1623);
    assert_or_panic(v[94] == 1624);
    assert_or_panic(v[95] == 1625);
    assert_or_panic(v[96] == 1626);
    assert_or_panic(v[97] == 1627);
    assert_or_panic(v[98] == 1628);
    assert_or_panic(v[99] == 1629);
    assert_or_panic(v[100] == 1630);
    assert_or_panic(v[101] == 1631);
    assert_or_panic(v[102] == 1632);
    assert_or_panic(v[103] == 1633);
    assert_or_panic(v[104] == 1634);
    assert_or_panic(v[105] == 1635);
    assert_or_panic(v[106] == 1636);
    assert_or_panic(v[107] == 1637);
    assert_or_panic(v[108] == 1638);
    assert_or_panic(v[109] == 1639);
    assert_or_panic(v[110] == 1640);
    assert_or_panic(v[111] == 1641);
    assert_or_panic(v[112] == 1642);
    assert_or_panic(v[113] == 1643);
    assert_or_panic(v[114] == 1644);
    assert_or_panic(v[115] == 1645);
    assert_or_panic(v[116] == 1646);
    assert_or_panic(v[117] == 1647);
    assert_or_panic(v[118] == 1648);
    assert_or_panic(v[119] == 1649);
    assert_or_panic(v[120] == 1650);
    assert_or_panic(v[121] == 1651);
    assert_or_panic(v[122] == 1652);
    assert_or_panic(v[123] == 1653);
    assert_or_panic(v[124] == 1654);
    assert_or_panic(v[125] == 1655);
    assert_or_panic(v[126] == 1656);
    assert_or_panic(v[127] == 1657);
    c_vector_128_u32((Vector_128_u32){
        1658, 1659, 1660, 1661, 1662, 1663, 1664, 1665, 1666, 1667, 1668, 1669, 1670, 1671, 1672, 1673,
        1674, 1675, 1676, 1677, 1678, 1679, 1680, 1681, 1682, 1683, 1684, 1685, 1686, 1687, 1688, 1689,
        1690, 1691, 1692, 1693, 1694, 1695, 1696, 1697, 1698, 1699, 1700, 1701, 1702, 1703, 1704, 1705,
        1706, 1707, 1708, 1709, 1710, 1711, 1712, 1713, 1714, 1715, 1716, 1717, 1718, 1719, 1720, 1721,
        1722, 1723, 1724, 1725, 1726, 1727, 1728, 1729, 1730, 1731, 1732, 1733, 1734, 1735, 1736, 1737,
        1738, 1739, 1740, 1741, 1742, 1743, 1744, 1745, 1746, 1747, 1748, 1749, 1750, 1751, 1752, 1753,
        1754, 1755, 1756, 1757, 1758, 1759, 1760, 1761, 1762, 1763, 1764, 1765, 1766, 1767, 1768, 1769,
        1770, 1771, 1772, 1773, 1774, 1775, 1776, 1777, 1778, 1779, 1780, 1781, 1782, 1783, 1784, 1785,
    }, 128);
    c_test_vector_128_u32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint64_t Vector_1_u64 __attribute__((vector_size(1 * sizeof(uint64_t))));
Vector_1_u64 zig_ret_vector_1_u64(void) {
    return (Vector_1_u64){1};
}
void zig_vector_1_u64(Vector_1_u64 v, size_t i) {
    assert_or_panic(v[0] == 2);
    assert_or_panic(i == 1);
}
Vector_1_u64 c_ret_vector_1_u64(void);
void c_vector_1_u64(Vector_1_u64, size_t);
void c_test_vector_1_u64(void);
static void test_vector_1_u64(void) {
    c_abi_current_test = "@Vector(1, u64)";
    Vector_1_u64 v = c_ret_vector_1_u64();
    assert_or_panic(v[0] == 3);
    c_vector_1_u64((Vector_1_u64){4}, 1);
    c_test_vector_1_u64();
}
typedef uint64_t Vector_2_u64 __attribute__((vector_size(2 * sizeof(uint64_t))));
Vector_2_u64 zig_ret_vector_2_u64(void) {
    return (Vector_2_u64){ 5, 6 };
}
void zig_vector_2_u64(Vector_2_u64 v, size_t i) {
    assert_or_panic(v[0] == 7);
    assert_or_panic(v[1] == 8);
    assert_or_panic(i == 2);
}
Vector_2_u64 c_ret_vector_2_u64(void);
void c_vector_2_u64(Vector_2_u64, size_t);
void c_test_vector_2_u64(void);
static void test_vector_2_u64(void) {
    c_abi_current_test = "@Vector(2, u64)";
    Vector_2_u64 v = c_ret_vector_2_u64();
    assert_or_panic(v[0] == 9);
    assert_or_panic(v[1] == 10);
    c_vector_2_u64((Vector_2_u64){ 11, 12 }, 2);
    c_test_vector_2_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint64_t Vector_3_u64 __attribute__((vector_size(3 * sizeof(uint64_t))));
Vector_3_u64 zig_ret_vector_3_u64(void) {
    return (Vector_3_u64){ 13, 14, 15 };
}
void zig_vector_3_u64(Vector_3_u64 v, size_t i) {
    assert_or_panic(v[0] == 16);
    assert_or_panic(v[1] == 17);
    assert_or_panic(v[2] == 18);
    assert_or_panic(i == 3);
}
Vector_3_u64 c_ret_vector_3_u64(void);
void c_vector_3_u64(Vector_3_u64, size_t);
void c_test_vector_3_u64(void);
static void test_vector_3_u64(void) {
    c_abi_current_test = "@Vector(3, u64)";
    Vector_3_u64 v = c_ret_vector_3_u64();
    assert_or_panic(v[0] == 19);
    assert_or_panic(v[1] == 20);
    assert_or_panic(v[2] == 21);
    c_vector_3_u64((Vector_3_u64){ 22, 23, 24 }, 3);
    c_test_vector_3_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint64_t Vector_4_u64 __attribute__((vector_size(4 * sizeof(uint64_t))));
Vector_4_u64 zig_ret_vector_4_u64(void) {
    return (Vector_4_u64){ 25, 26, 27, 28 };
}
void zig_vector_4_u64(Vector_4_u64 v, size_t i) {
    assert_or_panic(v[0] == 29);
    assert_or_panic(v[1] == 30);
    assert_or_panic(v[2] == 31);
    assert_or_panic(v[3] == 32);
    assert_or_panic(i == 4);
}
Vector_4_u64 c_ret_vector_4_u64(void);
void c_vector_4_u64(Vector_4_u64, size_t);
void c_test_vector_4_u64(void);
static void test_vector_4_u64(void) {
    c_abi_current_test = "@Vector(4, u64)";
    Vector_4_u64 v = c_ret_vector_4_u64();
    assert_or_panic(v[0] == 33);
    assert_or_panic(v[1] == 34);
    assert_or_panic(v[2] == 35);
    assert_or_panic(v[3] == 36);
    c_vector_4_u64((Vector_4_u64){ 37, 38, 39, 40 }, 4);
    c_test_vector_4_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef uint64_t Vector_6_u64 __attribute__((vector_size(6 * sizeof(uint64_t))));
Vector_6_u64 zig_ret_vector_6_u64(void) {
    return (Vector_6_u64){ 41, 42, 43, 44, 45, 46 };
}
void zig_vector_6_u64(Vector_6_u64 v, size_t i) {
    assert_or_panic(v[0] == 47);
    assert_or_panic(v[1] == 48);
    assert_or_panic(v[2] == 49);
    assert_or_panic(v[3] == 50);
    assert_or_panic(v[4] == 51);
    assert_or_panic(v[5] == 52);
    assert_or_panic(i == 6);
}
Vector_6_u64 c_ret_vector_6_u64(void);
void c_vector_6_u64(Vector_6_u64, size_t);
void c_test_vector_6_u64(void);
static void test_vector_6_u64(void) {
    c_abi_current_test = "@Vector(6, u64)";
    Vector_6_u64 v = c_ret_vector_6_u64();
    assert_or_panic(v[0] == 53);
    assert_or_panic(v[1] == 54);
    assert_or_panic(v[2] == 55);
    assert_or_panic(v[3] == 56);
    assert_or_panic(v[4] == 57);
    assert_or_panic(v[5] == 58);
    c_vector_6_u64((Vector_6_u64){ 59, 60, 61, 62, 63, 64 }, 6);
    c_test_vector_6_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef uint64_t Vector_8_u64 __attribute__((vector_size(8 * sizeof(uint64_t))));
Vector_8_u64 zig_ret_vector_8_u64(void) {
    return (Vector_8_u64){ 65, 66, 67, 68, 69, 70, 71, 72 };
}
void zig_vector_8_u64(Vector_8_u64 v, size_t i) {
    assert_or_panic(v[0] == 73);
    assert_or_panic(v[1] == 74);
    assert_or_panic(v[2] == 75);
    assert_or_panic(v[3] == 76);
    assert_or_panic(v[4] == 77);
    assert_or_panic(v[5] == 78);
    assert_or_panic(v[6] == 79);
    assert_or_panic(v[7] == 80);
    assert_or_panic(i == 8);
}
Vector_8_u64 c_ret_vector_8_u64(void);
void c_vector_8_u64(Vector_8_u64, size_t);
void c_test_vector_8_u64(void);
static void test_vector_8_u64(void) {
    c_abi_current_test = "@Vector(8, u64)";
    Vector_8_u64 v = c_ret_vector_8_u64();
    assert_or_panic(v[0] == 81);
    assert_or_panic(v[1] == 82);
    assert_or_panic(v[2] == 83);
    assert_or_panic(v[3] == 84);
    assert_or_panic(v[4] == 85);
    assert_or_panic(v[5] == 86);
    assert_or_panic(v[6] == 87);
    assert_or_panic(v[7] == 88);
    c_vector_8_u64((Vector_8_u64){ 89, 90, 91, 92, 93, 94, 95, 96 }, 8);
    c_test_vector_8_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint64_t Vector_12_u64 __attribute__((vector_size(12 * sizeof(uint64_t))));
Vector_12_u64 zig_ret_vector_12_u64(void) {
    return (Vector_12_u64){ 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108 };
}
void zig_vector_12_u64(Vector_12_u64 v, size_t i) {
    assert_or_panic(v[0] == 109);
    assert_or_panic(v[1] == 110);
    assert_or_panic(v[2] == 111);
    assert_or_panic(v[3] == 112);
    assert_or_panic(v[4] == 113);
    assert_or_panic(v[5] == 114);
    assert_or_panic(v[6] == 115);
    assert_or_panic(v[7] == 116);
    assert_or_panic(v[8] == 117);
    assert_or_panic(v[9] == 118);
    assert_or_panic(v[10] == 119);
    assert_or_panic(v[11] == 120);
    assert_or_panic(i == 12);
}
Vector_12_u64 c_ret_vector_12_u64(void);
void c_vector_12_u64(Vector_12_u64, size_t);
void c_test_vector_12_u64(void);
static void test_vector_12_u64(void) {
    c_abi_current_test = "@Vector(12, u64)";
    Vector_12_u64 v = c_ret_vector_12_u64();
    assert_or_panic(v[0] == 121);
    assert_or_panic(v[1] == 122);
    assert_or_panic(v[2] == 123);
    assert_or_panic(v[3] == 124);
    assert_or_panic(v[4] == 125);
    assert_or_panic(v[5] == 126);
    assert_or_panic(v[6] == 127);
    assert_or_panic(v[7] == 128);
    assert_or_panic(v[8] == 129);
    assert_or_panic(v[9] == 130);
    assert_or_panic(v[10] == 131);
    assert_or_panic(v[11] == 132);
    c_vector_12_u64((Vector_12_u64){ 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144 }, 12);
    c_test_vector_12_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint64_t Vector_16_u64 __attribute__((vector_size(16 * sizeof(uint64_t))));
Vector_16_u64 zig_ret_vector_16_u64(void) {
    return (Vector_16_u64){ 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160 };
}
void zig_vector_16_u64(Vector_16_u64 v, size_t i) {
    assert_or_panic(v[0] == 161);
    assert_or_panic(v[1] == 162);
    assert_or_panic(v[2] == 163);
    assert_or_panic(v[3] == 164);
    assert_or_panic(v[4] == 165);
    assert_or_panic(v[5] == 166);
    assert_or_panic(v[6] == 167);
    assert_or_panic(v[7] == 168);
    assert_or_panic(v[8] == 169);
    assert_or_panic(v[9] == 170);
    assert_or_panic(v[10] == 171);
    assert_or_panic(v[11] == 172);
    assert_or_panic(v[12] == 173);
    assert_or_panic(v[13] == 174);
    assert_or_panic(v[14] == 175);
    assert_or_panic(v[15] == 176);
    assert_or_panic(i == 16);
}
Vector_16_u64 c_ret_vector_16_u64(void);
void c_vector_16_u64(Vector_16_u64, size_t);
void c_test_vector_16_u64(void);
static void test_vector_16_u64(void) {
    c_abi_current_test = "@Vector(16, u64)";
    Vector_16_u64 v = c_ret_vector_16_u64();
    assert_or_panic(v[0] == 177);
    assert_or_panic(v[1] == 178);
    assert_or_panic(v[2] == 179);
    assert_or_panic(v[3] == 180);
    assert_or_panic(v[4] == 181);
    assert_or_panic(v[5] == 182);
    assert_or_panic(v[6] == 183);
    assert_or_panic(v[7] == 184);
    assert_or_panic(v[8] == 185);
    assert_or_panic(v[9] == 186);
    assert_or_panic(v[10] == 187);
    assert_or_panic(v[11] == 188);
    assert_or_panic(v[12] == 189);
    assert_or_panic(v[13] == 190);
    assert_or_panic(v[14] == 191);
    assert_or_panic(v[15] == 192);
    c_vector_16_u64((Vector_16_u64){ 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208 }, 16);
    c_test_vector_16_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint64_t Vector_24_u64 __attribute__((vector_size(24 * sizeof(uint64_t))));
Vector_24_u64 zig_ret_vector_24_u64(void) {
    return (Vector_24_u64){
    209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232,
    };
}
void zig_vector_24_u64(Vector_24_u64 v, size_t i) {
    assert_or_panic(v[0] == 233);
    assert_or_panic(v[1] == 234);
    assert_or_panic(v[2] == 235);
    assert_or_panic(v[3] == 236);
    assert_or_panic(v[4] == 237);
    assert_or_panic(v[5] == 238);
    assert_or_panic(v[6] == 239);
    assert_or_panic(v[7] == 240);
    assert_or_panic(v[8] == 241);
    assert_or_panic(v[9] == 242);
    assert_or_panic(v[10] == 243);
    assert_or_panic(v[11] == 244);
    assert_or_panic(v[12] == 245);
    assert_or_panic(v[13] == 246);
    assert_or_panic(v[14] == 247);
    assert_or_panic(v[15] == 248);
    assert_or_panic(v[16] == 249);
    assert_or_panic(v[17] == 250);
    assert_or_panic(v[18] == 251);
    assert_or_panic(v[19] == 252);
    assert_or_panic(v[20] == 253);
    assert_or_panic(v[21] == 254);
    assert_or_panic(v[22] == 255);
    assert_or_panic(v[23] == 256);
    assert_or_panic(i == 24);
}
Vector_24_u64 c_ret_vector_24_u64(void);
void c_vector_24_u64(Vector_24_u64, size_t);
void c_test_vector_24_u64(void);
static void test_vector_24_u64(void) {
    c_abi_current_test = "@Vector(24, u64)";
    Vector_24_u64 v = c_ret_vector_24_u64();
    assert_or_panic(v[0] == 257);
    assert_or_panic(v[1] == 258);
    assert_or_panic(v[2] == 259);
    assert_or_panic(v[3] == 260);
    assert_or_panic(v[4] == 261);
    assert_or_panic(v[5] == 262);
    assert_or_panic(v[6] == 263);
    assert_or_panic(v[7] == 264);
    assert_or_panic(v[8] == 265);
    assert_or_panic(v[9] == 266);
    assert_or_panic(v[10] == 267);
    assert_or_panic(v[11] == 268);
    assert_or_panic(v[12] == 269);
    assert_or_panic(v[13] == 270);
    assert_or_panic(v[14] == 271);
    assert_or_panic(v[15] == 272);
    assert_or_panic(v[16] == 273);
    assert_or_panic(v[17] == 274);
    assert_or_panic(v[18] == 275);
    assert_or_panic(v[19] == 276);
    assert_or_panic(v[20] == 277);
    assert_or_panic(v[21] == 278);
    assert_or_panic(v[22] == 279);
    assert_or_panic(v[23] == 280);
    c_vector_24_u64((Vector_24_u64){
        281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296,
        297, 298, 299, 300, 301, 302, 303, 304,
    }, 24);
    c_test_vector_24_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint64_t Vector_32_u64 __attribute__((vector_size(32 * sizeof(uint64_t))));
Vector_32_u64 zig_ret_vector_32_u64(void) {
    return (Vector_32_u64){
    305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320,
    321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336,
    };
}
void zig_vector_32_u64(Vector_32_u64 v, size_t i) {
    assert_or_panic(v[0] == 337);
    assert_or_panic(v[1] == 338);
    assert_or_panic(v[2] == 339);
    assert_or_panic(v[3] == 340);
    assert_or_panic(v[4] == 341);
    assert_or_panic(v[5] == 342);
    assert_or_panic(v[6] == 343);
    assert_or_panic(v[7] == 344);
    assert_or_panic(v[8] == 345);
    assert_or_panic(v[9] == 346);
    assert_or_panic(v[10] == 347);
    assert_or_panic(v[11] == 348);
    assert_or_panic(v[12] == 349);
    assert_or_panic(v[13] == 350);
    assert_or_panic(v[14] == 351);
    assert_or_panic(v[15] == 352);
    assert_or_panic(v[16] == 353);
    assert_or_panic(v[17] == 354);
    assert_or_panic(v[18] == 355);
    assert_or_panic(v[19] == 356);
    assert_or_panic(v[20] == 357);
    assert_or_panic(v[21] == 358);
    assert_or_panic(v[22] == 359);
    assert_or_panic(v[23] == 360);
    assert_or_panic(v[24] == 361);
    assert_or_panic(v[25] == 362);
    assert_or_panic(v[26] == 363);
    assert_or_panic(v[27] == 364);
    assert_or_panic(v[28] == 365);
    assert_or_panic(v[29] == 366);
    assert_or_panic(v[30] == 367);
    assert_or_panic(v[31] == 368);
    assert_or_panic(i == 32);
}
Vector_32_u64 c_ret_vector_32_u64(void);
void c_vector_32_u64(Vector_32_u64, size_t);
void c_test_vector_32_u64(void);
static void test_vector_32_u64(void) {
    c_abi_current_test = "@Vector(32, u64)";
    Vector_32_u64 v = c_ret_vector_32_u64();
    assert_or_panic(v[0] == 369);
    assert_or_panic(v[1] == 370);
    assert_or_panic(v[2] == 371);
    assert_or_panic(v[3] == 372);
    assert_or_panic(v[4] == 373);
    assert_or_panic(v[5] == 374);
    assert_or_panic(v[6] == 375);
    assert_or_panic(v[7] == 376);
    assert_or_panic(v[8] == 377);
    assert_or_panic(v[9] == 378);
    assert_or_panic(v[10] == 379);
    assert_or_panic(v[11] == 380);
    assert_or_panic(v[12] == 381);
    assert_or_panic(v[13] == 382);
    assert_or_panic(v[14] == 383);
    assert_or_panic(v[15] == 384);
    assert_or_panic(v[16] == 385);
    assert_or_panic(v[17] == 386);
    assert_or_panic(v[18] == 387);
    assert_or_panic(v[19] == 388);
    assert_or_panic(v[20] == 389);
    assert_or_panic(v[21] == 390);
    assert_or_panic(v[22] == 391);
    assert_or_panic(v[23] == 392);
    assert_or_panic(v[24] == 393);
    assert_or_panic(v[25] == 394);
    assert_or_panic(v[26] == 395);
    assert_or_panic(v[27] == 396);
    assert_or_panic(v[28] == 397);
    assert_or_panic(v[29] == 398);
    assert_or_panic(v[30] == 399);
    assert_or_panic(v[31] == 400);
    c_vector_32_u64((Vector_32_u64){
        401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416,
        417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432,
    }, 32);
    c_test_vector_32_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint64_t Vector_48_u64 __attribute__((vector_size(48 * sizeof(uint64_t))));
Vector_48_u64 zig_ret_vector_48_u64(void) {
    return (Vector_48_u64){
    433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448,
    449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464,
    465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    };
}
void zig_vector_48_u64(Vector_48_u64 v, size_t i) {
    assert_or_panic(v[0] == 481);
    assert_or_panic(v[1] == 482);
    assert_or_panic(v[2] == 483);
    assert_or_panic(v[3] == 484);
    assert_or_panic(v[4] == 485);
    assert_or_panic(v[5] == 486);
    assert_or_panic(v[6] == 487);
    assert_or_panic(v[7] == 488);
    assert_or_panic(v[8] == 489);
    assert_or_panic(v[9] == 490);
    assert_or_panic(v[10] == 491);
    assert_or_panic(v[11] == 492);
    assert_or_panic(v[12] == 493);
    assert_or_panic(v[13] == 494);
    assert_or_panic(v[14] == 495);
    assert_or_panic(v[15] == 496);
    assert_or_panic(v[16] == 497);
    assert_or_panic(v[17] == 498);
    assert_or_panic(v[18] == 499);
    assert_or_panic(v[19] == 500);
    assert_or_panic(v[20] == 501);
    assert_or_panic(v[21] == 502);
    assert_or_panic(v[22] == 503);
    assert_or_panic(v[23] == 504);
    assert_or_panic(v[24] == 505);
    assert_or_panic(v[25] == 506);
    assert_or_panic(v[26] == 507);
    assert_or_panic(v[27] == 508);
    assert_or_panic(v[28] == 509);
    assert_or_panic(v[29] == 510);
    assert_or_panic(v[30] == 511);
    assert_or_panic(v[31] == 512);
    assert_or_panic(v[32] == 513);
    assert_or_panic(v[33] == 514);
    assert_or_panic(v[34] == 515);
    assert_or_panic(v[35] == 516);
    assert_or_panic(v[36] == 517);
    assert_or_panic(v[37] == 518);
    assert_or_panic(v[38] == 519);
    assert_or_panic(v[39] == 520);
    assert_or_panic(v[40] == 521);
    assert_or_panic(v[41] == 522);
    assert_or_panic(v[42] == 523);
    assert_or_panic(v[43] == 524);
    assert_or_panic(v[44] == 525);
    assert_or_panic(v[45] == 526);
    assert_or_panic(v[46] == 527);
    assert_or_panic(v[47] == 528);
    assert_or_panic(i == 48);
}
Vector_48_u64 c_ret_vector_48_u64(void);
void c_vector_48_u64(Vector_48_u64, size_t);
void c_test_vector_48_u64(void);
static void test_vector_48_u64(void) {
    c_abi_current_test = "@Vector(48, u64)";
    Vector_48_u64 v = c_ret_vector_48_u64();
    assert_or_panic(v[0] == 529);
    assert_or_panic(v[1] == 530);
    assert_or_panic(v[2] == 531);
    assert_or_panic(v[3] == 532);
    assert_or_panic(v[4] == 533);
    assert_or_panic(v[5] == 534);
    assert_or_panic(v[6] == 535);
    assert_or_panic(v[7] == 536);
    assert_or_panic(v[8] == 537);
    assert_or_panic(v[9] == 538);
    assert_or_panic(v[10] == 539);
    assert_or_panic(v[11] == 540);
    assert_or_panic(v[12] == 541);
    assert_or_panic(v[13] == 542);
    assert_or_panic(v[14] == 543);
    assert_or_panic(v[15] == 544);
    assert_or_panic(v[16] == 545);
    assert_or_panic(v[17] == 546);
    assert_or_panic(v[18] == 547);
    assert_or_panic(v[19] == 548);
    assert_or_panic(v[20] == 549);
    assert_or_panic(v[21] == 550);
    assert_or_panic(v[22] == 551);
    assert_or_panic(v[23] == 552);
    assert_or_panic(v[24] == 553);
    assert_or_panic(v[25] == 554);
    assert_or_panic(v[26] == 555);
    assert_or_panic(v[27] == 556);
    assert_or_panic(v[28] == 557);
    assert_or_panic(v[29] == 558);
    assert_or_panic(v[30] == 559);
    assert_or_panic(v[31] == 560);
    assert_or_panic(v[32] == 561);
    assert_or_panic(v[33] == 562);
    assert_or_panic(v[34] == 563);
    assert_or_panic(v[35] == 564);
    assert_or_panic(v[36] == 565);
    assert_or_panic(v[37] == 566);
    assert_or_panic(v[38] == 567);
    assert_or_panic(v[39] == 568);
    assert_or_panic(v[40] == 569);
    assert_or_panic(v[41] == 570);
    assert_or_panic(v[42] == 571);
    assert_or_panic(v[43] == 572);
    assert_or_panic(v[44] == 573);
    assert_or_panic(v[45] == 574);
    assert_or_panic(v[46] == 575);
    assert_or_panic(v[47] == 576);
    c_vector_48_u64((Vector_48_u64){
        577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592,
        593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608,
        609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624,
    }, 48);
    c_test_vector_48_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef uint64_t Vector_64_u64 __attribute__((vector_size(64 * sizeof(uint64_t))));
Vector_64_u64 zig_ret_vector_64_u64(void) {
    return (Vector_64_u64){
    625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640,
    641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656,
    657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672,
    673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688,
    };
}
void zig_vector_64_u64(Vector_64_u64 v, size_t i) {
    assert_or_panic(v[0] == 689);
    assert_or_panic(v[1] == 690);
    assert_or_panic(v[2] == 691);
    assert_or_panic(v[3] == 692);
    assert_or_panic(v[4] == 693);
    assert_or_panic(v[5] == 694);
    assert_or_panic(v[6] == 695);
    assert_or_panic(v[7] == 696);
    assert_or_panic(v[8] == 697);
    assert_or_panic(v[9] == 698);
    assert_or_panic(v[10] == 699);
    assert_or_panic(v[11] == 700);
    assert_or_panic(v[12] == 701);
    assert_or_panic(v[13] == 702);
    assert_or_panic(v[14] == 703);
    assert_or_panic(v[15] == 704);
    assert_or_panic(v[16] == 705);
    assert_or_panic(v[17] == 706);
    assert_or_panic(v[18] == 707);
    assert_or_panic(v[19] == 708);
    assert_or_panic(v[20] == 709);
    assert_or_panic(v[21] == 710);
    assert_or_panic(v[22] == 711);
    assert_or_panic(v[23] == 712);
    assert_or_panic(v[24] == 713);
    assert_or_panic(v[25] == 714);
    assert_or_panic(v[26] == 715);
    assert_or_panic(v[27] == 716);
    assert_or_panic(v[28] == 717);
    assert_or_panic(v[29] == 718);
    assert_or_panic(v[30] == 719);
    assert_or_panic(v[31] == 720);
    assert_or_panic(v[32] == 721);
    assert_or_panic(v[33] == 722);
    assert_or_panic(v[34] == 723);
    assert_or_panic(v[35] == 724);
    assert_or_panic(v[36] == 725);
    assert_or_panic(v[37] == 726);
    assert_or_panic(v[38] == 727);
    assert_or_panic(v[39] == 728);
    assert_or_panic(v[40] == 729);
    assert_or_panic(v[41] == 730);
    assert_or_panic(v[42] == 731);
    assert_or_panic(v[43] == 732);
    assert_or_panic(v[44] == 733);
    assert_or_panic(v[45] == 734);
    assert_or_panic(v[46] == 735);
    assert_or_panic(v[47] == 736);
    assert_or_panic(v[48] == 737);
    assert_or_panic(v[49] == 738);
    assert_or_panic(v[50] == 739);
    assert_or_panic(v[51] == 740);
    assert_or_panic(v[52] == 741);
    assert_or_panic(v[53] == 742);
    assert_or_panic(v[54] == 743);
    assert_or_panic(v[55] == 744);
    assert_or_panic(v[56] == 745);
    assert_or_panic(v[57] == 746);
    assert_or_panic(v[58] == 747);
    assert_or_panic(v[59] == 748);
    assert_or_panic(v[60] == 749);
    assert_or_panic(v[61] == 750);
    assert_or_panic(v[62] == 751);
    assert_or_panic(v[63] == 752);
    assert_or_panic(i == 64);
}
Vector_64_u64 c_ret_vector_64_u64(void);
void c_vector_64_u64(Vector_64_u64, size_t);
void c_test_vector_64_u64(void);
static void test_vector_64_u64(void) {
    c_abi_current_test = "@Vector(64, u64)";
    Vector_64_u64 v = c_ret_vector_64_u64();
    assert_or_panic(v[0] == 753);
    assert_or_panic(v[1] == 754);
    assert_or_panic(v[2] == 755);
    assert_or_panic(v[3] == 756);
    assert_or_panic(v[4] == 757);
    assert_or_panic(v[5] == 758);
    assert_or_panic(v[6] == 759);
    assert_or_panic(v[7] == 760);
    assert_or_panic(v[8] == 761);
    assert_or_panic(v[9] == 762);
    assert_or_panic(v[10] == 763);
    assert_or_panic(v[11] == 764);
    assert_or_panic(v[12] == 765);
    assert_or_panic(v[13] == 766);
    assert_or_panic(v[14] == 767);
    assert_or_panic(v[15] == 768);
    assert_or_panic(v[16] == 769);
    assert_or_panic(v[17] == 770);
    assert_or_panic(v[18] == 771);
    assert_or_panic(v[19] == 772);
    assert_or_panic(v[20] == 773);
    assert_or_panic(v[21] == 774);
    assert_or_panic(v[22] == 775);
    assert_or_panic(v[23] == 776);
    assert_or_panic(v[24] == 777);
    assert_or_panic(v[25] == 778);
    assert_or_panic(v[26] == 779);
    assert_or_panic(v[27] == 780);
    assert_or_panic(v[28] == 781);
    assert_or_panic(v[29] == 782);
    assert_or_panic(v[30] == 783);
    assert_or_panic(v[31] == 784);
    assert_or_panic(v[32] == 785);
    assert_or_panic(v[33] == 786);
    assert_or_panic(v[34] == 787);
    assert_or_panic(v[35] == 788);
    assert_or_panic(v[36] == 789);
    assert_or_panic(v[37] == 790);
    assert_or_panic(v[38] == 791);
    assert_or_panic(v[39] == 792);
    assert_or_panic(v[40] == 793);
    assert_or_panic(v[41] == 794);
    assert_or_panic(v[42] == 795);
    assert_or_panic(v[43] == 796);
    assert_or_panic(v[44] == 797);
    assert_or_panic(v[45] == 798);
    assert_or_panic(v[46] == 799);
    assert_or_panic(v[47] == 800);
    assert_or_panic(v[48] == 801);
    assert_or_panic(v[49] == 802);
    assert_or_panic(v[50] == 803);
    assert_or_panic(v[51] == 804);
    assert_or_panic(v[52] == 805);
    assert_or_panic(v[53] == 806);
    assert_or_panic(v[54] == 807);
    assert_or_panic(v[55] == 808);
    assert_or_panic(v[56] == 809);
    assert_or_panic(v[57] == 810);
    assert_or_panic(v[58] == 811);
    assert_or_panic(v[59] == 812);
    assert_or_panic(v[60] == 813);
    assert_or_panic(v[61] == 814);
    assert_or_panic(v[62] == 815);
    assert_or_panic(v[63] == 816);
    c_vector_64_u64((Vector_64_u64){
        817, 818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 832,
        833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848,
        849, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864,
        865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880,
    }, 64);
    c_test_vector_64_u64();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef float Vector_1_f32 __attribute__((vector_size(1 * sizeof(float))));
Vector_1_f32 zig_ret_vector_1_f32(void) {
    return (Vector_1_f32){1};
}
void zig_vector_1_f32(Vector_1_f32 v, size_t i) {
    assert_or_panic(v[0] == 2);
    assert_or_panic(i == 1);
}
Vector_1_f32 c_ret_vector_1_f32(void);
void c_vector_1_f32(Vector_1_f32, size_t);
void c_test_vector_1_f32(void);
static void test_vector_1_f32(void) {
    c_abi_current_test = "@Vector(1, f32)";
#if !(defined(__aarch64__))
    Vector_1_f32 v = c_ret_vector_1_f32();
    assert_or_panic(v[0] == 3);
    c_vector_1_f32((Vector_1_f32){4}, 1);
    c_test_vector_1_f32();
#endif
}
typedef float Vector_2_f32 __attribute__((vector_size(2 * sizeof(float))));
Vector_2_f32 zig_ret_vector_2_f32(void) {
    return (Vector_2_f32){ 5, 6 };
}
void zig_vector_2_f32(Vector_2_f32 v, size_t i) {
    assert_or_panic(v[0] == 7);
    assert_or_panic(v[1] == 8);
    assert_or_panic(i == 2);
}
Vector_2_f32 c_ret_vector_2_f32(void);
void c_vector_2_f32(Vector_2_f32, size_t);
void c_test_vector_2_f32(void);
static void test_vector_2_f32(void) {
    c_abi_current_test = "@Vector(2, f32)";
    Vector_2_f32 v = c_ret_vector_2_f32();
    assert_or_panic(v[0] == 9);
    assert_or_panic(v[1] == 10);
    c_vector_2_f32((Vector_2_f32){ 11, 12 }, 2);
    c_test_vector_2_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef float Vector_3_f32 __attribute__((vector_size(3 * sizeof(float))));
Vector_3_f32 zig_ret_vector_3_f32(void) {
    return (Vector_3_f32){ 13, 14, 15 };
}
void zig_vector_3_f32(Vector_3_f32 v, size_t i) {
    assert_or_panic(v[0] == 16);
    assert_or_panic(v[1] == 17);
    assert_or_panic(v[2] == 18);
    assert_or_panic(i == 3);
}
Vector_3_f32 c_ret_vector_3_f32(void);
void c_vector_3_f32(Vector_3_f32, size_t);
void c_test_vector_3_f32(void);
static void test_vector_3_f32(void) {
    c_abi_current_test = "@Vector(3, f32)";
    Vector_3_f32 v = c_ret_vector_3_f32();
    assert_or_panic(v[0] == 19);
    assert_or_panic(v[1] == 20);
    assert_or_panic(v[2] == 21);
    c_vector_3_f32((Vector_3_f32){ 22, 23, 24 }, 32);
    c_test_vector_3_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef float Vector_4_f32 __attribute__((vector_size(4 * sizeof(float))));
Vector_4_f32 zig_ret_vector_4_f32(void) {
    return (Vector_4_f32){ 25, 26, 27, 28 };
}
void zig_vector_4_f32(Vector_4_f32 v, size_t i) {
    assert_or_panic(v[0] == 29);
    assert_or_panic(v[1] == 30);
    assert_or_panic(v[2] == 31);
    assert_or_panic(v[3] == 32);
    assert_or_panic(i == 4);
}
void zig_vector_4_f32_vector_4_f32(Vector_4_f32 v0, Vector_4_f32 v1, size_t i) {
    assert_or_panic(v0[0] == 33);
    assert_or_panic(v0[1] == 34);
    assert_or_panic(v0[2] == 35);
    assert_or_panic(v0[3] == 36);
    assert_or_panic(v1[0] == 37);
    assert_or_panic(v1[1] == 38);
    assert_or_panic(v1[2] == 39);
    assert_or_panic(v1[3] == 40);
    assert_or_panic(i == 8);
}
Vector_4_f32 c_ret_vector_4_f32(void);
void c_vector_4_f32(Vector_4_f32, size_t);
void c_vector_4_f32_vector_4_f32(Vector_4_f32, Vector_4_f32, size_t);
void c_test_vector_4_f32(void);
static void test_vector_4_f32(void) {
    c_abi_current_test = "@Vector(4, f32)";
    Vector_4_f32 v = c_ret_vector_4_f32();
    assert_or_panic(v[0] == 41);
    assert_or_panic(v[1] == 42);
    assert_or_panic(v[2] == 43);
    assert_or_panic(v[3] == 44);
    c_vector_4_f32((Vector_4_f32){ 45, 46, 47, 48 }, 4);
    c_vector_4_f32_vector_4_f32((Vector_4_f32){ 49, 50, 51, 52 }, (Vector_4_f32){ 53, 54, 55, 56 }, 8);
    c_test_vector_4_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef float Vector_6_f32 __attribute__((vector_size(6 * sizeof(float))));
Vector_6_f32 zig_ret_vector_6_f32(void) {
    return (Vector_6_f32){ 41, 42, 43, 44, 45, 46 };
}
void zig_vector_6_f32(Vector_6_f32 v, size_t i) {
    assert_or_panic(v[0] == 47);
    assert_or_panic(v[1] == 48);
    assert_or_panic(v[2] == 49);
    assert_or_panic(v[3] == 50);
    assert_or_panic(v[4] == 51);
    assert_or_panic(v[5] == 52);
    assert_or_panic(i == 6);
}
Vector_6_f32 c_ret_vector_6_f32(void);
void c_vector_6_f32(Vector_6_f32, size_t);
void c_test_vector_6_f32(void);
static void test_vector_6_f32(void) {
    c_abi_current_test = "@Vector(6, f32)";
    Vector_6_f32 v = c_ret_vector_6_f32();
    assert_or_panic(v[0] == 53);
    assert_or_panic(v[1] == 54);
    assert_or_panic(v[2] == 55);
    assert_or_panic(v[3] == 56);
    assert_or_panic(v[4] == 57);
    assert_or_panic(v[5] == 58);
    c_vector_6_f32((Vector_6_f32){ 59, 60, 61, 62, 63, 64 }, 6);
    c_test_vector_6_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef float Vector_8_f32 __attribute__((vector_size(8 * sizeof(float))));
Vector_8_f32 zig_ret_vector_8_f32(void) {
    return (Vector_8_f32){ 65, 66, 67, 68, 69, 70, 71, 72 };
}
void zig_vector_8_f32(Vector_8_f32 v, size_t i) {
    assert_or_panic(v[0] == 73);
    assert_or_panic(v[1] == 74);
    assert_or_panic(v[2] == 75);
    assert_or_panic(v[3] == 76);
    assert_or_panic(v[4] == 77);
    assert_or_panic(v[5] == 78);
    assert_or_panic(v[6] == 79);
    assert_or_panic(v[7] == 80);
    assert_or_panic(i == 8);
}
Vector_8_f32 c_ret_vector_8_f32(void);
void c_vector_8_f32(Vector_8_f32, size_t);
void c_test_vector_8_f32(void);
static void test_vector_8_f32(void) {
    c_abi_current_test = "@Vector(8, f32)";
    Vector_8_f32 v = c_ret_vector_8_f32();
    assert_or_panic(v[0] == 81);
    assert_or_panic(v[1] == 82);
    assert_or_panic(v[2] == 83);
    assert_or_panic(v[3] == 84);
    assert_or_panic(v[4] == 85);
    assert_or_panic(v[5] == 86);
    assert_or_panic(v[6] == 87);
    assert_or_panic(v[7] == 88);
    c_vector_8_f32((Vector_8_f32){ 89, 90, 91, 92, 93, 94, 95, 96 }, 8);
    c_test_vector_8_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef float Vector_12_f32 __attribute__((vector_size(12 * sizeof(float))));
Vector_12_f32 zig_ret_vector_12_f32(void) {
    return (Vector_12_f32){ 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108 };
}
void zig_vector_12_f32(Vector_12_f32 v, size_t i) {
    assert_or_panic(v[0] == 109);
    assert_or_panic(v[1] == 110);
    assert_or_panic(v[2] == 111);
    assert_or_panic(v[3] == 112);
    assert_or_panic(v[4] == 113);
    assert_or_panic(v[5] == 114);
    assert_or_panic(v[6] == 115);
    assert_or_panic(v[7] == 116);
    assert_or_panic(v[8] == 117);
    assert_or_panic(v[9] == 118);
    assert_or_panic(v[10] == 119);
    assert_or_panic(v[11] == 120);
    assert_or_panic(i == 12);
}
Vector_12_f32 c_ret_vector_12_f32(void);
void c_vector_12_f32(Vector_12_f32, size_t);
void c_test_vector_12_f32(void);
static void test_vector_12_f32(void) {
    c_abi_current_test = "@Vector(12, f32)";
    Vector_12_f32 v = c_ret_vector_12_f32();
    assert_or_panic(v[0] == 121);
    assert_or_panic(v[1] == 122);
    assert_or_panic(v[2] == 123);
    assert_or_panic(v[3] == 124);
    assert_or_panic(v[4] == 125);
    assert_or_panic(v[5] == 126);
    assert_or_panic(v[6] == 127);
    assert_or_panic(v[7] == 128);
    assert_or_panic(v[8] == 129);
    assert_or_panic(v[9] == 130);
    assert_or_panic(v[10] == 131);
    assert_or_panic(v[11] == 132);
    c_vector_12_f32((Vector_12_f32){ 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144 }, 12);
    c_test_vector_12_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef float Vector_16_f32 __attribute__((vector_size(16 * sizeof(float))));
Vector_16_f32 zig_ret_vector_16_f32(void) {
    return (Vector_16_f32){ 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160 };
}
void zig_vector_16_f32(Vector_16_f32 v, size_t i) {
    assert_or_panic(v[0] == 161);
    assert_or_panic(v[1] == 162);
    assert_or_panic(v[2] == 163);
    assert_or_panic(v[3] == 164);
    assert_or_panic(v[4] == 165);
    assert_or_panic(v[5] == 166);
    assert_or_panic(v[6] == 167);
    assert_or_panic(v[7] == 168);
    assert_or_panic(v[8] == 169);
    assert_or_panic(v[9] == 170);
    assert_or_panic(v[10] == 171);
    assert_or_panic(v[11] == 172);
    assert_or_panic(v[12] == 173);
    assert_or_panic(v[13] == 174);
    assert_or_panic(v[14] == 175);
    assert_or_panic(v[15] == 176);
    assert_or_panic(i == 16);
}
Vector_16_f32 c_ret_vector_16_f32(void);
void c_vector_16_f32(Vector_16_f32, size_t);
void c_test_vector_16_f32(void);
static void test_vector_16_f32(void) {
    c_abi_current_test = "@Vector(16, f32)";
    Vector_16_f32 v = c_ret_vector_16_f32();
    assert_or_panic(v[0] == 177);
    assert_or_panic(v[1] == 178);
    assert_or_panic(v[2] == 179);
    assert_or_panic(v[3] == 180);
    assert_or_panic(v[4] == 181);
    assert_or_panic(v[5] == 182);
    assert_or_panic(v[6] == 183);
    assert_or_panic(v[7] == 184);
    assert_or_panic(v[8] == 185);
    assert_or_panic(v[9] == 186);
    assert_or_panic(v[10] == 187);
    assert_or_panic(v[11] == 188);
    assert_or_panic(v[12] == 189);
    assert_or_panic(v[13] == 190);
    assert_or_panic(v[14] == 191);
    assert_or_panic(v[15] == 192);
    c_vector_16_f32((Vector_16_f32){ 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208 }, 16);
    c_test_vector_16_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef float Vector_24_f32 __attribute__((vector_size(24 * sizeof(float))));
Vector_24_f32 zig_ret_vector_24_f32(void) {
    return (Vector_24_f32){
    209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232,
    };
}
void zig_vector_24_f32(Vector_24_f32 v, size_t i) {
    assert_or_panic(v[0] == 233);
    assert_or_panic(v[1] == 234);
    assert_or_panic(v[2] == 235);
    assert_or_panic(v[3] == 236);
    assert_or_panic(v[4] == 237);
    assert_or_panic(v[5] == 238);
    assert_or_panic(v[6] == 239);
    assert_or_panic(v[7] == 240);
    assert_or_panic(v[8] == 241);
    assert_or_panic(v[9] == 242);
    assert_or_panic(v[10] == 243);
    assert_or_panic(v[11] == 244);
    assert_or_panic(v[12] == 245);
    assert_or_panic(v[13] == 246);
    assert_or_panic(v[14] == 247);
    assert_or_panic(v[15] == 248);
    assert_or_panic(v[16] == 249);
    assert_or_panic(v[17] == 250);
    assert_or_panic(v[18] == 251);
    assert_or_panic(v[19] == 252);
    assert_or_panic(v[20] == 253);
    assert_or_panic(v[21] == 254);
    assert_or_panic(v[22] == 255);
    assert_or_panic(v[23] == 256);
    assert_or_panic(i == 24);
}
Vector_24_f32 c_ret_vector_24_f32(void);
void c_vector_24_f32(Vector_24_f32, size_t);
void c_test_vector_24_f32(void);
static void test_vector_24_f32(void) {
    c_abi_current_test = "@Vector(24, f32)";
    Vector_24_f32 v = c_ret_vector_24_f32();
    assert_or_panic(v[0] == 257);
    assert_or_panic(v[1] == 258);
    assert_or_panic(v[2] == 259);
    assert_or_panic(v[3] == 260);
    assert_or_panic(v[4] == 261);
    assert_or_panic(v[5] == 262);
    assert_or_panic(v[6] == 263);
    assert_or_panic(v[7] == 264);
    assert_or_panic(v[8] == 265);
    assert_or_panic(v[9] == 266);
    assert_or_panic(v[10] == 267);
    assert_or_panic(v[11] == 268);
    assert_or_panic(v[12] == 269);
    assert_or_panic(v[13] == 270);
    assert_or_panic(v[14] == 271);
    assert_or_panic(v[15] == 272);
    assert_or_panic(v[16] == 273);
    assert_or_panic(v[17] == 274);
    assert_or_panic(v[18] == 275);
    assert_or_panic(v[19] == 276);
    assert_or_panic(v[20] == 277);
    assert_or_panic(v[21] == 278);
    assert_or_panic(v[22] == 279);
    assert_or_panic(v[23] == 280);
    c_vector_24_f32((Vector_24_f32){
        281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296,
        297, 298, 299, 300, 301, 302, 303, 304,
    }, 24);
    c_test_vector_24_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef float Vector_32_f32 __attribute__((vector_size(32 * sizeof(float))));
Vector_32_f32 zig_ret_vector_32_f32(void) {
    return (Vector_32_f32){
    305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320,
    321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336,
    };
}
void zig_vector_32_f32(Vector_32_f32 v, size_t i) {
    assert_or_panic(v[0] == 337);
    assert_or_panic(v[1] == 338);
    assert_or_panic(v[2] == 339);
    assert_or_panic(v[3] == 340);
    assert_or_panic(v[4] == 341);
    assert_or_panic(v[5] == 342);
    assert_or_panic(v[6] == 343);
    assert_or_panic(v[7] == 344);
    assert_or_panic(v[8] == 345);
    assert_or_panic(v[9] == 346);
    assert_or_panic(v[10] == 347);
    assert_or_panic(v[11] == 348);
    assert_or_panic(v[12] == 349);
    assert_or_panic(v[13] == 350);
    assert_or_panic(v[14] == 351);
    assert_or_panic(v[15] == 352);
    assert_or_panic(v[16] == 353);
    assert_or_panic(v[17] == 354);
    assert_or_panic(v[18] == 355);
    assert_or_panic(v[19] == 356);
    assert_or_panic(v[20] == 357);
    assert_or_panic(v[21] == 358);
    assert_or_panic(v[22] == 359);
    assert_or_panic(v[23] == 360);
    assert_or_panic(v[24] == 361);
    assert_or_panic(v[25] == 362);
    assert_or_panic(v[26] == 363);
    assert_or_panic(v[27] == 364);
    assert_or_panic(v[28] == 365);
    assert_or_panic(v[29] == 366);
    assert_or_panic(v[30] == 367);
    assert_or_panic(v[31] == 368);
    assert_or_panic(i == 32);
}
Vector_32_f32 c_ret_vector_32_f32(void);
void c_vector_32_f32(Vector_32_f32, size_t);
void c_test_vector_32_f32(void);
static void test_vector_32_f32(void) {
    c_abi_current_test = "@Vector(32, f32)";
    Vector_32_f32 v = c_ret_vector_32_f32();
    assert_or_panic(v[0] == 369);
    assert_or_panic(v[1] == 370);
    assert_or_panic(v[2] == 371);
    assert_or_panic(v[3] == 372);
    assert_or_panic(v[4] == 373);
    assert_or_panic(v[5] == 374);
    assert_or_panic(v[6] == 375);
    assert_or_panic(v[7] == 376);
    assert_or_panic(v[8] == 377);
    assert_or_panic(v[9] == 378);
    assert_or_panic(v[10] == 379);
    assert_or_panic(v[11] == 380);
    assert_or_panic(v[12] == 381);
    assert_or_panic(v[13] == 382);
    assert_or_panic(v[14] == 383);
    assert_or_panic(v[15] == 384);
    assert_or_panic(v[16] == 385);
    assert_or_panic(v[17] == 386);
    assert_or_panic(v[18] == 387);
    assert_or_panic(v[19] == 388);
    assert_or_panic(v[20] == 389);
    assert_or_panic(v[21] == 390);
    assert_or_panic(v[22] == 391);
    assert_or_panic(v[23] == 392);
    assert_or_panic(v[24] == 393);
    assert_or_panic(v[25] == 394);
    assert_or_panic(v[26] == 395);
    assert_or_panic(v[27] == 396);
    assert_or_panic(v[28] == 397);
    assert_or_panic(v[29] == 398);
    assert_or_panic(v[30] == 399);
    assert_or_panic(v[31] == 400);
    c_vector_32_f32((Vector_32_f32){
        401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416,
        417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432,
    }, 32);
    c_test_vector_32_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef float Vector_48_f32 __attribute__((vector_size(48 * sizeof(float))));
Vector_48_f32 zig_ret_vector_48_f32(void) {
    return (Vector_48_f32){
    433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448,
    449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464,
    465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    };
}
void zig_vector_48_f32(Vector_48_f32 v, size_t i) {
    assert_or_panic(v[0] == 481);
    assert_or_panic(v[1] == 482);
    assert_or_panic(v[2] == 483);
    assert_or_panic(v[3] == 484);
    assert_or_panic(v[4] == 485);
    assert_or_panic(v[5] == 486);
    assert_or_panic(v[6] == 487);
    assert_or_panic(v[7] == 488);
    assert_or_panic(v[8] == 489);
    assert_or_panic(v[9] == 490);
    assert_or_panic(v[10] == 491);
    assert_or_panic(v[11] == 492);
    assert_or_panic(v[12] == 493);
    assert_or_panic(v[13] == 494);
    assert_or_panic(v[14] == 495);
    assert_or_panic(v[15] == 496);
    assert_or_panic(v[16] == 497);
    assert_or_panic(v[17] == 498);
    assert_or_panic(v[18] == 499);
    assert_or_panic(v[19] == 500);
    assert_or_panic(v[20] == 501);
    assert_or_panic(v[21] == 502);
    assert_or_panic(v[22] == 503);
    assert_or_panic(v[23] == 504);
    assert_or_panic(v[24] == 505);
    assert_or_panic(v[25] == 506);
    assert_or_panic(v[26] == 507);
    assert_or_panic(v[27] == 508);
    assert_or_panic(v[28] == 509);
    assert_or_panic(v[29] == 510);
    assert_or_panic(v[30] == 511);
    assert_or_panic(v[31] == 512);
    assert_or_panic(v[32] == 513);
    assert_or_panic(v[33] == 514);
    assert_or_panic(v[34] == 515);
    assert_or_panic(v[35] == 516);
    assert_or_panic(v[36] == 517);
    assert_or_panic(v[37] == 518);
    assert_or_panic(v[38] == 519);
    assert_or_panic(v[39] == 520);
    assert_or_panic(v[40] == 521);
    assert_or_panic(v[41] == 522);
    assert_or_panic(v[42] == 523);
    assert_or_panic(v[43] == 524);
    assert_or_panic(v[44] == 525);
    assert_or_panic(v[45] == 526);
    assert_or_panic(v[46] == 527);
    assert_or_panic(v[47] == 528);
    assert_or_panic(i == 48);
}
Vector_48_f32 c_ret_vector_48_f32(void);
void c_vector_48_f32(Vector_48_f32, size_t);
void c_test_vector_48_f32(void);
static void test_vector_48_f32(void) {
    c_abi_current_test = "@Vector(48, f32)";
    Vector_48_f32 v = c_ret_vector_48_f32();
    assert_or_panic(v[0] == 529);
    assert_or_panic(v[1] == 530);
    assert_or_panic(v[2] == 531);
    assert_or_panic(v[3] == 532);
    assert_or_panic(v[4] == 533);
    assert_or_panic(v[5] == 534);
    assert_or_panic(v[6] == 535);
    assert_or_panic(v[7] == 536);
    assert_or_panic(v[8] == 537);
    assert_or_panic(v[9] == 538);
    assert_or_panic(v[10] == 539);
    assert_or_panic(v[11] == 540);
    assert_or_panic(v[12] == 541);
    assert_or_panic(v[13] == 542);
    assert_or_panic(v[14] == 543);
    assert_or_panic(v[15] == 544);
    assert_or_panic(v[16] == 545);
    assert_or_panic(v[17] == 546);
    assert_or_panic(v[18] == 547);
    assert_or_panic(v[19] == 548);
    assert_or_panic(v[20] == 549);
    assert_or_panic(v[21] == 550);
    assert_or_panic(v[22] == 551);
    assert_or_panic(v[23] == 552);
    assert_or_panic(v[24] == 553);
    assert_or_panic(v[25] == 554);
    assert_or_panic(v[26] == 555);
    assert_or_panic(v[27] == 556);
    assert_or_panic(v[28] == 557);
    assert_or_panic(v[29] == 558);
    assert_or_panic(v[30] == 559);
    assert_or_panic(v[31] == 560);
    assert_or_panic(v[32] == 561);
    assert_or_panic(v[33] == 562);
    assert_or_panic(v[34] == 563);
    assert_or_panic(v[35] == 564);
    assert_or_panic(v[36] == 565);
    assert_or_panic(v[37] == 566);
    assert_or_panic(v[38] == 567);
    assert_or_panic(v[39] == 568);
    assert_or_panic(v[40] == 569);
    assert_or_panic(v[41] == 570);
    assert_or_panic(v[42] == 571);
    assert_or_panic(v[43] == 572);
    assert_or_panic(v[44] == 573);
    assert_or_panic(v[45] == 574);
    assert_or_panic(v[46] == 575);
    assert_or_panic(v[47] == 576);
    c_vector_48_f32((Vector_48_f32){
        577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592,
        593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608,
        609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624,
    }, 48);
    c_test_vector_48_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef float Vector_64_f32 __attribute__((vector_size(64 * sizeof(float))));
Vector_64_f32 zig_ret_vector_64_f32(void) {
    return (Vector_64_f32){
    625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640,
    641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656,
    657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672,
    673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688,
    };
}
void zig_vector_64_f32(Vector_64_f32 v, size_t i) {
    assert_or_panic(v[0] == 689);
    assert_or_panic(v[1] == 690);
    assert_or_panic(v[2] == 691);
    assert_or_panic(v[3] == 692);
    assert_or_panic(v[4] == 693);
    assert_or_panic(v[5] == 694);
    assert_or_panic(v[6] == 695);
    assert_or_panic(v[7] == 696);
    assert_or_panic(v[8] == 697);
    assert_or_panic(v[9] == 698);
    assert_or_panic(v[10] == 699);
    assert_or_panic(v[11] == 700);
    assert_or_panic(v[12] == 701);
    assert_or_panic(v[13] == 702);
    assert_or_panic(v[14] == 703);
    assert_or_panic(v[15] == 704);
    assert_or_panic(v[16] == 705);
    assert_or_panic(v[17] == 706);
    assert_or_panic(v[18] == 707);
    assert_or_panic(v[19] == 708);
    assert_or_panic(v[20] == 709);
    assert_or_panic(v[21] == 710);
    assert_or_panic(v[22] == 711);
    assert_or_panic(v[23] == 712);
    assert_or_panic(v[24] == 713);
    assert_or_panic(v[25] == 714);
    assert_or_panic(v[26] == 715);
    assert_or_panic(v[27] == 716);
    assert_or_panic(v[28] == 717);
    assert_or_panic(v[29] == 718);
    assert_or_panic(v[30] == 719);
    assert_or_panic(v[31] == 720);
    assert_or_panic(v[32] == 721);
    assert_or_panic(v[33] == 722);
    assert_or_panic(v[34] == 723);
    assert_or_panic(v[35] == 724);
    assert_or_panic(v[36] == 725);
    assert_or_panic(v[37] == 726);
    assert_or_panic(v[38] == 727);
    assert_or_panic(v[39] == 728);
    assert_or_panic(v[40] == 729);
    assert_or_panic(v[41] == 730);
    assert_or_panic(v[42] == 731);
    assert_or_panic(v[43] == 732);
    assert_or_panic(v[44] == 733);
    assert_or_panic(v[45] == 734);
    assert_or_panic(v[46] == 735);
    assert_or_panic(v[47] == 736);
    assert_or_panic(v[48] == 737);
    assert_or_panic(v[49] == 738);
    assert_or_panic(v[50] == 739);
    assert_or_panic(v[51] == 740);
    assert_or_panic(v[52] == 741);
    assert_or_panic(v[53] == 742);
    assert_or_panic(v[54] == 743);
    assert_or_panic(v[55] == 744);
    assert_or_panic(v[56] == 745);
    assert_or_panic(v[57] == 746);
    assert_or_panic(v[58] == 747);
    assert_or_panic(v[59] == 748);
    assert_or_panic(v[60] == 749);
    assert_or_panic(v[61] == 750);
    assert_or_panic(v[62] == 751);
    assert_or_panic(v[63] == 752);
    assert_or_panic(i == 64);
}
Vector_64_f32 c_ret_vector_64_f32(void);
void c_vector_64_f32(Vector_64_f32, size_t);
void c_test_vector_64_f32(void);
static void test_vector_64_f32(void) {
    c_abi_current_test = "@Vector(64, f32)";
    Vector_64_f32 v = c_ret_vector_64_f32();
    assert_or_panic(v[0] == 753);
    assert_or_panic(v[1] == 754);
    assert_or_panic(v[2] == 755);
    assert_or_panic(v[3] == 756);
    assert_or_panic(v[4] == 757);
    assert_or_panic(v[5] == 758);
    assert_or_panic(v[6] == 759);
    assert_or_panic(v[7] == 760);
    assert_or_panic(v[8] == 761);
    assert_or_panic(v[9] == 762);
    assert_or_panic(v[10] == 763);
    assert_or_panic(v[11] == 764);
    assert_or_panic(v[12] == 765);
    assert_or_panic(v[13] == 766);
    assert_or_panic(v[14] == 767);
    assert_or_panic(v[15] == 768);
    assert_or_panic(v[16] == 769);
    assert_or_panic(v[17] == 770);
    assert_or_panic(v[18] == 771);
    assert_or_panic(v[19] == 772);
    assert_or_panic(v[20] == 773);
    assert_or_panic(v[21] == 774);
    assert_or_panic(v[22] == 775);
    assert_or_panic(v[23] == 776);
    assert_or_panic(v[24] == 777);
    assert_or_panic(v[25] == 778);
    assert_or_panic(v[26] == 779);
    assert_or_panic(v[27] == 780);
    assert_or_panic(v[28] == 781);
    assert_or_panic(v[29] == 782);
    assert_or_panic(v[30] == 783);
    assert_or_panic(v[31] == 784);
    assert_or_panic(v[32] == 785);
    assert_or_panic(v[33] == 786);
    assert_or_panic(v[34] == 787);
    assert_or_panic(v[35] == 788);
    assert_or_panic(v[36] == 789);
    assert_or_panic(v[37] == 790);
    assert_or_panic(v[38] == 791);
    assert_or_panic(v[39] == 792);
    assert_or_panic(v[40] == 793);
    assert_or_panic(v[41] == 794);
    assert_or_panic(v[42] == 795);
    assert_or_panic(v[43] == 796);
    assert_or_panic(v[44] == 797);
    assert_or_panic(v[45] == 798);
    assert_or_panic(v[46] == 799);
    assert_or_panic(v[47] == 800);
    assert_or_panic(v[48] == 801);
    assert_or_panic(v[49] == 802);
    assert_or_panic(v[50] == 803);
    assert_or_panic(v[51] == 804);
    assert_or_panic(v[52] == 805);
    assert_or_panic(v[53] == 806);
    assert_or_panic(v[54] == 807);
    assert_or_panic(v[55] == 808);
    assert_or_panic(v[56] == 809);
    assert_or_panic(v[57] == 810);
    assert_or_panic(v[58] == 811);
    assert_or_panic(v[59] == 812);
    assert_or_panic(v[60] == 813);
    assert_or_panic(v[61] == 814);
    assert_or_panic(v[62] == 815);
    assert_or_panic(v[63] == 816);
    c_vector_64_f32((Vector_64_f32){
        817, 818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 832,
        833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848,
        849, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864,
        865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880,
    }, 64);
    c_test_vector_64_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef float Vector_96_f32 __attribute__((vector_size(96 * sizeof(float))));
Vector_96_f32 zig_ret_vector_96_f32(void) {
    return (Vector_96_f32){
    890, 891, 892, 893, 894, 895, 896, 897, 898, 899, 900, 901, 902, 903, 904, 905,
    906, 907, 908, 909, 910, 911, 912, 913, 914, 915, 916, 917, 918, 919, 920, 921,
    922, 923, 924, 925, 926, 927, 928, 929, 930, 931, 932, 933, 934, 935, 936, 937,
    938, 939, 940, 941, 942, 943, 944, 945, 946, 947, 948, 949, 950, 951, 952, 953,
    954, 955, 956, 957, 958, 959, 960, 961, 962, 963, 964, 965, 966, 967, 968, 969,
    970, 971, 972, 973, 974, 975, 976, 977, 978, 979, 980, 981, 982, 983, 984, 985,
    };
}
void zig_vector_96_f32(Vector_96_f32 v, size_t i) {
    assert_or_panic(v[0] == 986);
    assert_or_panic(v[1] == 987);
    assert_or_panic(v[2] == 988);
    assert_or_panic(v[3] == 989);
    assert_or_panic(v[4] == 990);
    assert_or_panic(v[5] == 991);
    assert_or_panic(v[6] == 992);
    assert_or_panic(v[7] == 993);
    assert_or_panic(v[8] == 994);
    assert_or_panic(v[9] == 995);
    assert_or_panic(v[10] == 996);
    assert_or_panic(v[11] == 997);
    assert_or_panic(v[12] == 998);
    assert_or_panic(v[13] == 999);
    assert_or_panic(v[14] == 1000);
    assert_or_panic(v[15] == 1001);
    assert_or_panic(v[16] == 1002);
    assert_or_panic(v[17] == 1003);
    assert_or_panic(v[18] == 1004);
    assert_or_panic(v[19] == 1005);
    assert_or_panic(v[20] == 1006);
    assert_or_panic(v[21] == 1007);
    assert_or_panic(v[22] == 1008);
    assert_or_panic(v[23] == 1009);
    assert_or_panic(v[24] == 1010);
    assert_or_panic(v[25] == 1011);
    assert_or_panic(v[26] == 1012);
    assert_or_panic(v[27] == 1013);
    assert_or_panic(v[28] == 1014);
    assert_or_panic(v[29] == 1015);
    assert_or_panic(v[30] == 1016);
    assert_or_panic(v[31] == 1017);
    assert_or_panic(v[32] == 1018);
    assert_or_panic(v[33] == 1019);
    assert_or_panic(v[34] == 1020);
    assert_or_panic(v[35] == 1021);
    assert_or_panic(v[36] == 1022);
    assert_or_panic(v[37] == 1023);
    assert_or_panic(v[38] == 1024);
    assert_or_panic(v[39] == 1025);
    assert_or_panic(v[40] == 1026);
    assert_or_panic(v[41] == 1027);
    assert_or_panic(v[42] == 1028);
    assert_or_panic(v[43] == 1029);
    assert_or_panic(v[44] == 1030);
    assert_or_panic(v[45] == 1031);
    assert_or_panic(v[46] == 1032);
    assert_or_panic(v[47] == 1033);
    assert_or_panic(v[48] == 1034);
    assert_or_panic(v[49] == 1035);
    assert_or_panic(v[50] == 1036);
    assert_or_panic(v[51] == 1037);
    assert_or_panic(v[52] == 1038);
    assert_or_panic(v[53] == 1039);
    assert_or_panic(v[54] == 1040);
    assert_or_panic(v[55] == 1041);
    assert_or_panic(v[56] == 1042);
    assert_or_panic(v[57] == 1043);
    assert_or_panic(v[58] == 1044);
    assert_or_panic(v[59] == 1045);
    assert_or_panic(v[60] == 1046);
    assert_or_panic(v[61] == 1047);
    assert_or_panic(v[62] == 1048);
    assert_or_panic(v[63] == 1049);
    assert_or_panic(v[64] == 1050);
    assert_or_panic(v[65] == 1051);
    assert_or_panic(v[66] == 1052);
    assert_or_panic(v[67] == 1053);
    assert_or_panic(v[68] == 1054);
    assert_or_panic(v[69] == 1055);
    assert_or_panic(v[70] == 1056);
    assert_or_panic(v[71] == 1057);
    assert_or_panic(v[72] == 1058);
    assert_or_panic(v[73] == 1059);
    assert_or_panic(v[74] == 1060);
    assert_or_panic(v[75] == 1061);
    assert_or_panic(v[76] == 1062);
    assert_or_panic(v[77] == 1063);
    assert_or_panic(v[78] == 1064);
    assert_or_panic(v[79] == 1065);
    assert_or_panic(v[80] == 1066);
    assert_or_panic(v[81] == 1067);
    assert_or_panic(v[82] == 1068);
    assert_or_panic(v[83] == 1069);
    assert_or_panic(v[84] == 1070);
    assert_or_panic(v[85] == 1071);
    assert_or_panic(v[86] == 1072);
    assert_or_panic(v[87] == 1073);
    assert_or_panic(v[88] == 1074);
    assert_or_panic(v[89] == 1075);
    assert_or_panic(v[90] == 1076);
    assert_or_panic(v[91] == 1077);
    assert_or_panic(v[92] == 1078);
    assert_or_panic(v[93] == 1079);
    assert_or_panic(v[94] == 1080);
    assert_or_panic(v[95] == 1081);
    assert_or_panic(i == 96);
}
Vector_96_f32 c_ret_vector_96_f32(void);
void c_vector_96_f32(Vector_96_f32, size_t);
void c_test_vector_96_f32(void);
static void test_vector_96_f32(void) {
    c_abi_current_test = "@Vector(96, f32)";
    Vector_96_f32 v = c_ret_vector_96_f32();
    assert_or_panic(v[0] == 1082);
    assert_or_panic(v[1] == 1083);
    assert_or_panic(v[2] == 1084);
    assert_or_panic(v[3] == 1085);
    assert_or_panic(v[4] == 1086);
    assert_or_panic(v[5] == 1087);
    assert_or_panic(v[6] == 1088);
    assert_or_panic(v[7] == 1089);
    assert_or_panic(v[8] == 1090);
    assert_or_panic(v[9] == 1091);
    assert_or_panic(v[10] == 1092);
    assert_or_panic(v[11] == 1093);
    assert_or_panic(v[12] == 1094);
    assert_or_panic(v[13] == 1095);
    assert_or_panic(v[14] == 1096);
    assert_or_panic(v[15] == 1097);
    assert_or_panic(v[16] == 1098);
    assert_or_panic(v[17] == 1099);
    assert_or_panic(v[18] == 1100);
    assert_or_panic(v[19] == 1101);
    assert_or_panic(v[20] == 1102);
    assert_or_panic(v[21] == 1103);
    assert_or_panic(v[22] == 1104);
    assert_or_panic(v[23] == 1105);
    assert_or_panic(v[24] == 1106);
    assert_or_panic(v[25] == 1107);
    assert_or_panic(v[26] == 1108);
    assert_or_panic(v[27] == 1109);
    assert_or_panic(v[28] == 1110);
    assert_or_panic(v[29] == 1111);
    assert_or_panic(v[30] == 1112);
    assert_or_panic(v[31] == 1113);
    assert_or_panic(v[32] == 1114);
    assert_or_panic(v[33] == 1115);
    assert_or_panic(v[34] == 1116);
    assert_or_panic(v[35] == 1117);
    assert_or_panic(v[36] == 1118);
    assert_or_panic(v[37] == 1119);
    assert_or_panic(v[38] == 1120);
    assert_or_panic(v[39] == 1121);
    assert_or_panic(v[40] == 1122);
    assert_or_panic(v[41] == 1123);
    assert_or_panic(v[42] == 1124);
    assert_or_panic(v[43] == 1125);
    assert_or_panic(v[44] == 1126);
    assert_or_panic(v[45] == 1127);
    assert_or_panic(v[46] == 1128);
    assert_or_panic(v[47] == 1129);
    assert_or_panic(v[48] == 1130);
    assert_or_panic(v[49] == 1131);
    assert_or_panic(v[50] == 1132);
    assert_or_panic(v[51] == 1133);
    assert_or_panic(v[52] == 1134);
    assert_or_panic(v[53] == 1135);
    assert_or_panic(v[54] == 1136);
    assert_or_panic(v[55] == 1137);
    assert_or_panic(v[56] == 1138);
    assert_or_panic(v[57] == 1139);
    assert_or_panic(v[58] == 1140);
    assert_or_panic(v[59] == 1141);
    assert_or_panic(v[60] == 1142);
    assert_or_panic(v[61] == 1143);
    assert_or_panic(v[62] == 1144);
    assert_or_panic(v[63] == 1145);
    assert_or_panic(v[64] == 1146);
    assert_or_panic(v[65] == 1147);
    assert_or_panic(v[66] == 1148);
    assert_or_panic(v[67] == 1149);
    assert_or_panic(v[68] == 1150);
    assert_or_panic(v[69] == 1151);
    assert_or_panic(v[70] == 1152);
    assert_or_panic(v[71] == 1153);
    assert_or_panic(v[72] == 1154);
    assert_or_panic(v[73] == 1155);
    assert_or_panic(v[74] == 1156);
    assert_or_panic(v[75] == 1157);
    assert_or_panic(v[76] == 1158);
    assert_or_panic(v[77] == 1159);
    assert_or_panic(v[78] == 1160);
    assert_or_panic(v[79] == 1161);
    assert_or_panic(v[80] == 1162);
    assert_or_panic(v[81] == 1163);
    assert_or_panic(v[82] == 1164);
    assert_or_panic(v[83] == 1165);
    assert_or_panic(v[84] == 1166);
    assert_or_panic(v[85] == 1167);
    assert_or_panic(v[86] == 1168);
    assert_or_panic(v[87] == 1169);
    assert_or_panic(v[88] == 1170);
    assert_or_panic(v[89] == 1171);
    assert_or_panic(v[90] == 1172);
    assert_or_panic(v[91] == 1173);
    assert_or_panic(v[92] == 1174);
    assert_or_panic(v[93] == 1175);
    assert_or_panic(v[94] == 1176);
    assert_or_panic(v[95] == 1177);
    c_vector_96_f32((Vector_96_f32){
        1178, 1179, 1180, 1181, 1182, 1183, 1184, 1185, 1186, 1187, 1188, 1189, 1190, 1191, 1192, 1193,
        1194, 1195, 1196, 1197, 1198, 1199, 1200, 1201, 1202, 1203, 1204, 1205, 1206, 1207, 1208, 1209,
        1210, 1211, 1212, 1213, 1214, 1215, 1216, 1217, 1218, 1219, 1220, 1221, 1222, 1223, 1224, 1225,
        1226, 1227, 1228, 1229, 1230, 1231, 1232, 1233, 1234, 1235, 1236, 1237, 1238, 1239, 1240, 1241,
        1242, 1243, 1244, 1245, 1246, 1247, 1248, 1249, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257,
        1258, 1259, 1260, 1261, 1262, 1263, 1264, 1265, 1266, 1267, 1268, 1269, 1270, 1271, 1272, 1273,
    }, 96);
    c_test_vector_96_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef float Vector_128_f32 __attribute__((vector_size(128 * sizeof(float))));
Vector_128_f32 zig_ret_vector_128_f32(void) {
    return (Vector_128_f32){
    1274, 1275, 1276, 1277, 1278, 1279, 1280, 1281, 1282, 1283, 1284, 1285, 1286, 1287, 1288, 1289,
    1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1298, 1299, 1300, 1301, 1302, 1303, 1304, 1305,
    1306, 1307, 1308, 1309, 1310, 1311, 1312, 1313, 1314, 1315, 1316, 1317, 1318, 1319, 1320, 1321,
    1322, 1323, 1324, 1325, 1326, 1327, 1328, 1329, 1330, 1331, 1332, 1333, 1334, 1335, 1336, 1337,
    1338, 1339, 1340, 1341, 1342, 1343, 1344, 1345, 1346, 1347, 1348, 1349, 1350, 1351, 1352, 1353,
    1354, 1355, 1356, 1357, 1358, 1359, 1360, 1361, 1362, 1363, 1364, 1365, 1366, 1367, 1368, 1369,
    1370, 1371, 1372, 1373, 1374, 1375, 1376, 1377, 1378, 1379, 1380, 1381, 1382, 1383, 1384, 1385,
    1386, 1387, 1388, 1389, 1390, 1391, 1392, 1393, 1394, 1395, 1396, 1397, 1398, 1399, 1400, 1401,
    };
}
void zig_vector_128_f32(Vector_128_f32 v, size_t i) {
    assert_or_panic(v[0] == 1402);
    assert_or_panic(v[1] == 1403);
    assert_or_panic(v[2] == 1404);
    assert_or_panic(v[3] == 1405);
    assert_or_panic(v[4] == 1406);
    assert_or_panic(v[5] == 1407);
    assert_or_panic(v[6] == 1408);
    assert_or_panic(v[7] == 1409);
    assert_or_panic(v[8] == 1410);
    assert_or_panic(v[9] == 1411);
    assert_or_panic(v[10] == 1412);
    assert_or_panic(v[11] == 1413);
    assert_or_panic(v[12] == 1414);
    assert_or_panic(v[13] == 1415);
    assert_or_panic(v[14] == 1416);
    assert_or_panic(v[15] == 1417);
    assert_or_panic(v[16] == 1418);
    assert_or_panic(v[17] == 1419);
    assert_or_panic(v[18] == 1420);
    assert_or_panic(v[19] == 1421);
    assert_or_panic(v[20] == 1422);
    assert_or_panic(v[21] == 1423);
    assert_or_panic(v[22] == 1424);
    assert_or_panic(v[23] == 1425);
    assert_or_panic(v[24] == 1426);
    assert_or_panic(v[25] == 1427);
    assert_or_panic(v[26] == 1428);
    assert_or_panic(v[27] == 1429);
    assert_or_panic(v[28] == 1430);
    assert_or_panic(v[29] == 1431);
    assert_or_panic(v[30] == 1432);
    assert_or_panic(v[31] == 1433);
    assert_or_panic(v[32] == 1434);
    assert_or_panic(v[33] == 1435);
    assert_or_panic(v[34] == 1436);
    assert_or_panic(v[35] == 1437);
    assert_or_panic(v[36] == 1438);
    assert_or_panic(v[37] == 1439);
    assert_or_panic(v[38] == 1440);
    assert_or_panic(v[39] == 1441);
    assert_or_panic(v[40] == 1442);
    assert_or_panic(v[41] == 1443);
    assert_or_panic(v[42] == 1444);
    assert_or_panic(v[43] == 1445);
    assert_or_panic(v[44] == 1446);
    assert_or_panic(v[45] == 1447);
    assert_or_panic(v[46] == 1448);
    assert_or_panic(v[47] == 1449);
    assert_or_panic(v[48] == 1450);
    assert_or_panic(v[49] == 1451);
    assert_or_panic(v[50] == 1452);
    assert_or_panic(v[51] == 1453);
    assert_or_panic(v[52] == 1454);
    assert_or_panic(v[53] == 1455);
    assert_or_panic(v[54] == 1456);
    assert_or_panic(v[55] == 1457);
    assert_or_panic(v[56] == 1458);
    assert_or_panic(v[57] == 1459);
    assert_or_panic(v[58] == 1460);
    assert_or_panic(v[59] == 1461);
    assert_or_panic(v[60] == 1462);
    assert_or_panic(v[61] == 1463);
    assert_or_panic(v[62] == 1464);
    assert_or_panic(v[63] == 1465);
    assert_or_panic(v[64] == 1466);
    assert_or_panic(v[65] == 1467);
    assert_or_panic(v[66] == 1468);
    assert_or_panic(v[67] == 1469);
    assert_or_panic(v[68] == 1470);
    assert_or_panic(v[69] == 1471);
    assert_or_panic(v[70] == 1472);
    assert_or_panic(v[71] == 1473);
    assert_or_panic(v[72] == 1474);
    assert_or_panic(v[73] == 1475);
    assert_or_panic(v[74] == 1476);
    assert_or_panic(v[75] == 1477);
    assert_or_panic(v[76] == 1478);
    assert_or_panic(v[77] == 1479);
    assert_or_panic(v[78] == 1480);
    assert_or_panic(v[79] == 1481);
    assert_or_panic(v[80] == 1482);
    assert_or_panic(v[81] == 1483);
    assert_or_panic(v[82] == 1484);
    assert_or_panic(v[83] == 1485);
    assert_or_panic(v[84] == 1486);
    assert_or_panic(v[85] == 1487);
    assert_or_panic(v[86] == 1488);
    assert_or_panic(v[87] == 1489);
    assert_or_panic(v[88] == 1490);
    assert_or_panic(v[89] == 1491);
    assert_or_panic(v[90] == 1492);
    assert_or_panic(v[91] == 1493);
    assert_or_panic(v[92] == 1494);
    assert_or_panic(v[93] == 1495);
    assert_or_panic(v[94] == 1496);
    assert_or_panic(v[95] == 1497);
    assert_or_panic(v[96] == 1498);
    assert_or_panic(v[97] == 1499);
    assert_or_panic(v[98] == 1500);
    assert_or_panic(v[99] == 1501);
    assert_or_panic(v[100] == 1502);
    assert_or_panic(v[101] == 1503);
    assert_or_panic(v[102] == 1504);
    assert_or_panic(v[103] == 1505);
    assert_or_panic(v[104] == 1506);
    assert_or_panic(v[105] == 1507);
    assert_or_panic(v[106] == 1508);
    assert_or_panic(v[107] == 1509);
    assert_or_panic(v[108] == 1510);
    assert_or_panic(v[109] == 1511);
    assert_or_panic(v[110] == 1512);
    assert_or_panic(v[111] == 1513);
    assert_or_panic(v[112] == 1514);
    assert_or_panic(v[113] == 1515);
    assert_or_panic(v[114] == 1516);
    assert_or_panic(v[115] == 1517);
    assert_or_panic(v[116] == 1518);
    assert_or_panic(v[117] == 1519);
    assert_or_panic(v[118] == 1520);
    assert_or_panic(v[119] == 1521);
    assert_or_panic(v[120] == 1522);
    assert_or_panic(v[121] == 1523);
    assert_or_panic(v[122] == 1524);
    assert_or_panic(v[123] == 1525);
    assert_or_panic(v[124] == 1526);
    assert_or_panic(v[125] == 1527);
    assert_or_panic(v[126] == 1528);
    assert_or_panic(v[127] == 1529);
    assert_or_panic(i == 128);
}
Vector_128_f32 c_ret_vector_128_f32(void);
void c_vector_128_f32(Vector_128_f32, size_t);
void c_test_vector_128_f32(void);
static void test_vector_128_f32(void) {
    c_abi_current_test = "@Vector(128, f32)";
    Vector_128_f32 v = c_ret_vector_128_f32();
    assert_or_panic(v[0] == 1530);
    assert_or_panic(v[1] == 1531);
    assert_or_panic(v[2] == 1532);
    assert_or_panic(v[3] == 1533);
    assert_or_panic(v[4] == 1534);
    assert_or_panic(v[5] == 1535);
    assert_or_panic(v[6] == 1536);
    assert_or_panic(v[7] == 1537);
    assert_or_panic(v[8] == 1538);
    assert_or_panic(v[9] == 1539);
    assert_or_panic(v[10] == 1540);
    assert_or_panic(v[11] == 1541);
    assert_or_panic(v[12] == 1542);
    assert_or_panic(v[13] == 1543);
    assert_or_panic(v[14] == 1544);
    assert_or_panic(v[15] == 1545);
    assert_or_panic(v[16] == 1546);
    assert_or_panic(v[17] == 1547);
    assert_or_panic(v[18] == 1548);
    assert_or_panic(v[19] == 1549);
    assert_or_panic(v[20] == 1550);
    assert_or_panic(v[21] == 1551);
    assert_or_panic(v[22] == 1552);
    assert_or_panic(v[23] == 1553);
    assert_or_panic(v[24] == 1554);
    assert_or_panic(v[25] == 1555);
    assert_or_panic(v[26] == 1556);
    assert_or_panic(v[27] == 1557);
    assert_or_panic(v[28] == 1558);
    assert_or_panic(v[29] == 1559);
    assert_or_panic(v[30] == 1560);
    assert_or_panic(v[31] == 1561);
    assert_or_panic(v[32] == 1562);
    assert_or_panic(v[33] == 1563);
    assert_or_panic(v[34] == 1564);
    assert_or_panic(v[35] == 1565);
    assert_or_panic(v[36] == 1566);
    assert_or_panic(v[37] == 1567);
    assert_or_panic(v[38] == 1568);
    assert_or_panic(v[39] == 1569);
    assert_or_panic(v[40] == 1570);
    assert_or_panic(v[41] == 1571);
    assert_or_panic(v[42] == 1572);
    assert_or_panic(v[43] == 1573);
    assert_or_panic(v[44] == 1574);
    assert_or_panic(v[45] == 1575);
    assert_or_panic(v[46] == 1576);
    assert_or_panic(v[47] == 1577);
    assert_or_panic(v[48] == 1578);
    assert_or_panic(v[49] == 1579);
    assert_or_panic(v[50] == 1580);
    assert_or_panic(v[51] == 1581);
    assert_or_panic(v[52] == 1582);
    assert_or_panic(v[53] == 1583);
    assert_or_panic(v[54] == 1584);
    assert_or_panic(v[55] == 1585);
    assert_or_panic(v[56] == 1586);
    assert_or_panic(v[57] == 1587);
    assert_or_panic(v[58] == 1588);
    assert_or_panic(v[59] == 1589);
    assert_or_panic(v[60] == 1590);
    assert_or_panic(v[61] == 1591);
    assert_or_panic(v[62] == 1592);
    assert_or_panic(v[63] == 1593);
    assert_or_panic(v[64] == 1594);
    assert_or_panic(v[65] == 1595);
    assert_or_panic(v[66] == 1596);
    assert_or_panic(v[67] == 1597);
    assert_or_panic(v[68] == 1598);
    assert_or_panic(v[69] == 1599);
    assert_or_panic(v[70] == 1600);
    assert_or_panic(v[71] == 1601);
    assert_or_panic(v[72] == 1602);
    assert_or_panic(v[73] == 1603);
    assert_or_panic(v[74] == 1604);
    assert_or_panic(v[75] == 1605);
    assert_or_panic(v[76] == 1606);
    assert_or_panic(v[77] == 1607);
    assert_or_panic(v[78] == 1608);
    assert_or_panic(v[79] == 1609);
    assert_or_panic(v[80] == 1610);
    assert_or_panic(v[81] == 1611);
    assert_or_panic(v[82] == 1612);
    assert_or_panic(v[83] == 1613);
    assert_or_panic(v[84] == 1614);
    assert_or_panic(v[85] == 1615);
    assert_or_panic(v[86] == 1616);
    assert_or_panic(v[87] == 1617);
    assert_or_panic(v[88] == 1618);
    assert_or_panic(v[89] == 1619);
    assert_or_panic(v[90] == 1620);
    assert_or_panic(v[91] == 1621);
    assert_or_panic(v[92] == 1622);
    assert_or_panic(v[93] == 1623);
    assert_or_panic(v[94] == 1624);
    assert_or_panic(v[95] == 1625);
    assert_or_panic(v[96] == 1626);
    assert_or_panic(v[97] == 1627);
    assert_or_panic(v[98] == 1628);
    assert_or_panic(v[99] == 1629);
    assert_or_panic(v[100] == 1630);
    assert_or_panic(v[101] == 1631);
    assert_or_panic(v[102] == 1632);
    assert_or_panic(v[103] == 1633);
    assert_or_panic(v[104] == 1634);
    assert_or_panic(v[105] == 1635);
    assert_or_panic(v[106] == 1636);
    assert_or_panic(v[107] == 1637);
    assert_or_panic(v[108] == 1638);
    assert_or_panic(v[109] == 1639);
    assert_or_panic(v[110] == 1640);
    assert_or_panic(v[111] == 1641);
    assert_or_panic(v[112] == 1642);
    assert_or_panic(v[113] == 1643);
    assert_or_panic(v[114] == 1644);
    assert_or_panic(v[115] == 1645);
    assert_or_panic(v[116] == 1646);
    assert_or_panic(v[117] == 1647);
    assert_or_panic(v[118] == 1648);
    assert_or_panic(v[119] == 1649);
    assert_or_panic(v[120] == 1650);
    assert_or_panic(v[121] == 1651);
    assert_or_panic(v[122] == 1652);
    assert_or_panic(v[123] == 1653);
    assert_or_panic(v[124] == 1654);
    assert_or_panic(v[125] == 1655);
    assert_or_panic(v[126] == 1656);
    assert_or_panic(v[127] == 1657);
    c_vector_128_f32((Vector_128_f32){
        1658, 1659, 1660, 1661, 1662, 1663, 1664, 1665, 1666, 1667, 1668, 1669, 1670, 1671, 1672, 1673,
        1674, 1675, 1676, 1677, 1678, 1679, 1680, 1681, 1682, 1683, 1684, 1685, 1686, 1687, 1688, 1689,
        1690, 1691, 1692, 1693, 1694, 1695, 1696, 1697, 1698, 1699, 1700, 1701, 1702, 1703, 1704, 1705,
        1706, 1707, 1708, 1709, 1710, 1711, 1712, 1713, 1714, 1715, 1716, 1717, 1718, 1719, 1720, 1721,
        1722, 1723, 1724, 1725, 1726, 1727, 1728, 1729, 1730, 1731, 1732, 1733, 1734, 1735, 1736, 1737,
        1738, 1739, 1740, 1741, 1742, 1743, 1744, 1745, 1746, 1747, 1748, 1749, 1750, 1751, 1752, 1753,
        1754, 1755, 1756, 1757, 1758, 1759, 1760, 1761, 1762, 1763, 1764, 1765, 1766, 1767, 1768, 1769,
        1770, 1771, 1772, 1773, 1774, 1775, 1776, 1777, 1778, 1779, 1780, 1781, 1782, 1783, 1784, 1785,
    }, 128);
    c_test_vector_128_f32();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef double Vector_1_f64 __attribute__((vector_size(1 * sizeof(double))));
Vector_1_f64 zig_ret_vector_1_f64(void) {
    return (Vector_1_f64){1};
}
void zig_vector_1_f64(Vector_1_f64 v, size_t i) {
    assert_or_panic(v[0] == 2);
    assert_or_panic(i == 1);
}
Vector_1_f64 c_ret_vector_1_f64(void);
void c_vector_1_f64(Vector_1_f64, size_t);
void c_test_vector_1_f64(void);
static void test_vector_1_f64(void) {
    c_abi_current_test = "@Vector(1, f64)";
    Vector_1_f64 v = c_ret_vector_1_f64();
    assert_or_panic(v[0] == 3);
    c_vector_1_f64((Vector_1_f64){4}, 1);
    c_test_vector_1_f64();
}
typedef double Vector_2_f64 __attribute__((vector_size(2 * sizeof(double))));
Vector_2_f64 zig_ret_vector_2_f64(void) {
    return (Vector_2_f64){ 5, 6 };
}
void zig_vector_2_f64(Vector_2_f64 v, size_t i) {
    assert_or_panic(v[0] == 7);
    assert_or_panic(v[1] == 8);
    assert_or_panic(i == 2);
}
Vector_2_f64 c_ret_vector_2_f64(void);
void c_vector_2_f64(Vector_2_f64, size_t);
void c_test_vector_2_f64(void);
static void test_vector_2_f64(void) {
    c_abi_current_test = "@Vector(2, f64)";
    Vector_2_f64 v = c_ret_vector_2_f64();
    assert_or_panic(v[0] == 9);
    assert_or_panic(v[1] == 10);
    c_vector_2_f64((Vector_2_f64){ 11, 12 }, 2);
    c_test_vector_2_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef double Vector_3_f64 __attribute__((vector_size(3 * sizeof(double))));
Vector_3_f64 zig_ret_vector_3_f64(void) {
    return (Vector_3_f64){ 13, 14, 15 };
}
void zig_vector_3_f64(Vector_3_f64 v, size_t i) {
    assert_or_panic(v[0] == 16);
    assert_or_panic(v[1] == 17);
    assert_or_panic(v[2] == 18);
    assert_or_panic(i == 3);
}
Vector_3_f64 c_ret_vector_3_f64(void);
void c_vector_3_f64(Vector_3_f64, size_t);
void c_test_vector_3_f64(void);
static void test_vector_3_f64(void) {
    c_abi_current_test = "@Vector(3, f64)";
    Vector_3_f64 v = c_ret_vector_3_f64();
    assert_or_panic(v[0] == 19);
    assert_or_panic(v[1] == 20);
    assert_or_panic(v[2] == 21);
    c_vector_3_f64((Vector_3_f64){ 22, 23, 24 }, 3);
    c_test_vector_3_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef double Vector_4_f64 __attribute__((vector_size(4 * sizeof(double))));
Vector_4_f64 zig_ret_vector_4_f64(void) {
    return (Vector_4_f64){ 25, 26, 27, 28 };
}
void zig_vector_4_f64(Vector_4_f64 v, size_t i) {
    assert_or_panic(v[0] == 29);
    assert_or_panic(v[1] == 30);
    assert_or_panic(v[2] == 31);
    assert_or_panic(v[3] == 32);
    assert_or_panic(i == 4);
}
Vector_4_f64 c_ret_vector_4_f64(void);
void c_vector_4_f64(Vector_4_f64, size_t);
void c_test_vector_4_f64(void);
static void test_vector_4_f64(void) {
    c_abi_current_test = "@Vector(4, f64)";
    Vector_4_f64 v = c_ret_vector_4_f64();
    assert_or_panic(v[0] == 33);
    assert_or_panic(v[1] == 34);
    assert_or_panic(v[2] == 35);
    assert_or_panic(v[3] == 36);
    c_vector_4_f64((Vector_4_f64){ 37, 38, 39, 40 }, 4);
    c_test_vector_4_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
typedef double Vector_6_f64 __attribute__((vector_size(6 * sizeof(double))));
Vector_6_f64 zig_ret_vector_6_f64(void) {
    return (Vector_6_f64){ 41, 42, 43, 44, 45, 46 };
}
void zig_vector_6_f64(Vector_6_f64 v, size_t i) {
    assert_or_panic(v[0] == 47);
    assert_or_panic(v[1] == 48);
    assert_or_panic(v[2] == 49);
    assert_or_panic(v[3] == 50);
    assert_or_panic(v[4] == 51);
    assert_or_panic(v[5] == 52);
    assert_or_panic(i == 6);
}
Vector_6_f64 c_ret_vector_6_f64(void);
void c_vector_6_f64(Vector_6_f64, size_t);
void c_test_vector_6_f64(void);
static void test_vector_6_f64(void) {
    c_abi_current_test = "@Vector(6, f64)";
    Vector_6_f64 v = c_ret_vector_6_f64();
    assert_or_panic(v[0] == 53);
    assert_or_panic(v[1] == 54);
    assert_or_panic(v[2] == 55);
    assert_or_panic(v[3] == 56);
    assert_or_panic(v[4] == 57);
    assert_or_panic(v[5] == 58);
    c_vector_6_f64((Vector_6_f64){ 59, 60, 61, 62, 63, 64 }, 6);
    c_test_vector_6_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS)
typedef double Vector_8_f64 __attribute__((vector_size(8 * sizeof(double))));
Vector_8_f64 zig_ret_vector_8_f64(void) {
    return (Vector_8_f64){ 65, 66, 67, 68, 69, 70, 71, 72 };
}
void zig_vector_8_f64(Vector_8_f64 v, size_t i) {
    assert_or_panic(v[0] == 73);
    assert_or_panic(v[1] == 74);
    assert_or_panic(v[2] == 75);
    assert_or_panic(v[3] == 76);
    assert_or_panic(v[4] == 77);
    assert_or_panic(v[5] == 78);
    assert_or_panic(v[6] == 79);
    assert_or_panic(v[7] == 80);
    assert_or_panic(i == 8);
}
Vector_8_f64 c_ret_vector_8_f64(void);
void c_vector_8_f64(Vector_8_f64, size_t);
void c_test_vector_8_f64(void);
static void test_vector_8_f64(void) {
    c_abi_current_test = "@Vector(8, f64)";
    Vector_8_f64 v = c_ret_vector_8_f64();
    assert_or_panic(v[0] == 81);
    assert_or_panic(v[1] == 82);
    assert_or_panic(v[2] == 83);
    assert_or_panic(v[3] == 84);
    assert_or_panic(v[4] == 85);
    assert_or_panic(v[5] == 86);
    assert_or_panic(v[6] == 87);
    assert_or_panic(v[7] == 88);
    c_vector_8_f64((Vector_8_f64){ 89, 90, 91, 92, 93, 94, 95, 96 }, 8);
    c_test_vector_8_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef double Vector_12_f64 __attribute__((vector_size(12 * sizeof(double))));
Vector_12_f64 zig_ret_vector_12_f64(void) {
    return (Vector_12_f64){ 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108 };
}
void zig_vector_12_f64(Vector_12_f64 v, size_t i) {
    assert_or_panic(v[0] == 109);
    assert_or_panic(v[1] == 110);
    assert_or_panic(v[2] == 111);
    assert_or_panic(v[3] == 112);
    assert_or_panic(v[4] == 113);
    assert_or_panic(v[5] == 114);
    assert_or_panic(v[6] == 115);
    assert_or_panic(v[7] == 116);
    assert_or_panic(v[8] == 117);
    assert_or_panic(v[9] == 118);
    assert_or_panic(v[10] == 119);
    assert_or_panic(v[11] == 120);
    assert_or_panic(i == 12);
}
Vector_12_f64 c_ret_vector_12_f64(void);
void c_vector_12_f64(Vector_12_f64, size_t);
void c_test_vector_12_f64(void);
static void test_vector_12_f64(void) {
    c_abi_current_test = "@Vector(12, f64)";
    Vector_12_f64 v = c_ret_vector_12_f64();
    assert_or_panic(v[0] == 121);
    assert_or_panic(v[1] == 122);
    assert_or_panic(v[2] == 123);
    assert_or_panic(v[3] == 124);
    assert_or_panic(v[4] == 125);
    assert_or_panic(v[5] == 126);
    assert_or_panic(v[6] == 127);
    assert_or_panic(v[7] == 128);
    assert_or_panic(v[8] == 129);
    assert_or_panic(v[9] == 130);
    assert_or_panic(v[10] == 131);
    assert_or_panic(v[11] == 132);
    c_vector_12_f64((Vector_12_f64){ 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144 }, 12);
    c_test_vector_12_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef double Vector_16_f64 __attribute__((vector_size(16 * sizeof(double))));
Vector_16_f64 zig_ret_vector_16_f64(void) {
    return (Vector_16_f64){ 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160 };
}
void zig_vector_16_f64(Vector_16_f64 v, size_t i) {
    assert_or_panic(v[0] == 161);
    assert_or_panic(v[1] == 162);
    assert_or_panic(v[2] == 163);
    assert_or_panic(v[3] == 164);
    assert_or_panic(v[4] == 165);
    assert_or_panic(v[5] == 166);
    assert_or_panic(v[6] == 167);
    assert_or_panic(v[7] == 168);
    assert_or_panic(v[8] == 169);
    assert_or_panic(v[9] == 170);
    assert_or_panic(v[10] == 171);
    assert_or_panic(v[11] == 172);
    assert_or_panic(v[12] == 173);
    assert_or_panic(v[13] == 174);
    assert_or_panic(v[14] == 175);
    assert_or_panic(v[15] == 176);
    assert_or_panic(i == 16);
}
Vector_16_f64 c_ret_vector_16_f64(void);
void c_vector_16_f64(Vector_16_f64, size_t);
void c_test_vector_16_f64(void);
static void test_vector_16_f64(void) {
    c_abi_current_test = "@Vector(16, f64)";
    Vector_16_f64 v = c_ret_vector_16_f64();
    assert_or_panic(v[0] == 177);
    assert_or_panic(v[1] == 178);
    assert_or_panic(v[2] == 179);
    assert_or_panic(v[3] == 180);
    assert_or_panic(v[4] == 181);
    assert_or_panic(v[5] == 182);
    assert_or_panic(v[6] == 183);
    assert_or_panic(v[7] == 184);
    assert_or_panic(v[8] == 185);
    assert_or_panic(v[9] == 186);
    assert_or_panic(v[10] == 187);
    assert_or_panic(v[11] == 188);
    assert_or_panic(v[12] == 189);
    assert_or_panic(v[13] == 190);
    assert_or_panic(v[14] == 191);
    assert_or_panic(v[15] == 192);
    c_vector_16_f64((Vector_16_f64){ 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208 }, 16);
    c_test_vector_16_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef double Vector_24_f64 __attribute__((vector_size(24 * sizeof(double))));
Vector_24_f64 zig_ret_vector_24_f64(void) {
    return (Vector_24_f64){
    209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232,
    };
}
void zig_vector_24_f64(Vector_24_f64 v, size_t i) {
    assert_or_panic(v[0] == 233);
    assert_or_panic(v[1] == 234);
    assert_or_panic(v[2] == 235);
    assert_or_panic(v[3] == 236);
    assert_or_panic(v[4] == 237);
    assert_or_panic(v[5] == 238);
    assert_or_panic(v[6] == 239);
    assert_or_panic(v[7] == 240);
    assert_or_panic(v[8] == 241);
    assert_or_panic(v[9] == 242);
    assert_or_panic(v[10] == 243);
    assert_or_panic(v[11] == 244);
    assert_or_panic(v[12] == 245);
    assert_or_panic(v[13] == 246);
    assert_or_panic(v[14] == 247);
    assert_or_panic(v[15] == 248);
    assert_or_panic(v[16] == 249);
    assert_or_panic(v[17] == 250);
    assert_or_panic(v[18] == 251);
    assert_or_panic(v[19] == 252);
    assert_or_panic(v[20] == 253);
    assert_or_panic(v[21] == 254);
    assert_or_panic(v[22] == 255);
    assert_or_panic(v[23] == 256);
    assert_or_panic(i == 24);
}
Vector_24_f64 c_ret_vector_24_f64(void);
void c_vector_24_f64(Vector_24_f64, size_t);
void c_test_vector_24_f64(void);
static void test_vector_24_f64(void) {
    c_abi_current_test = "@Vector(24, f64)";
    Vector_24_f64 v = c_ret_vector_24_f64();
    assert_or_panic(v[0] == 257);
    assert_or_panic(v[1] == 258);
    assert_or_panic(v[2] == 259);
    assert_or_panic(v[3] == 260);
    assert_or_panic(v[4] == 261);
    assert_or_panic(v[5] == 262);
    assert_or_panic(v[6] == 263);
    assert_or_panic(v[7] == 264);
    assert_or_panic(v[8] == 265);
    assert_or_panic(v[9] == 266);
    assert_or_panic(v[10] == 267);
    assert_or_panic(v[11] == 268);
    assert_or_panic(v[12] == 269);
    assert_or_panic(v[13] == 270);
    assert_or_panic(v[14] == 271);
    assert_or_panic(v[15] == 272);
    assert_or_panic(v[16] == 273);
    assert_or_panic(v[17] == 274);
    assert_or_panic(v[18] == 275);
    assert_or_panic(v[19] == 276);
    assert_or_panic(v[20] == 277);
    assert_or_panic(v[21] == 278);
    assert_or_panic(v[22] == 279);
    assert_or_panic(v[23] == 280);
    c_vector_24_f64((Vector_24_f64){
        281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296,
        297, 298, 299, 300, 301, 302, 303, 304,
    }, 24);
    c_test_vector_24_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef double Vector_32_f64 __attribute__((vector_size(32 * sizeof(double))));
Vector_32_f64 zig_ret_vector_32_f64(void) {
    return (Vector_32_f64){
    305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320,
    321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336,
    };
}
void zig_vector_32_f64(Vector_32_f64 v, size_t i) {
    assert_or_panic(v[0] == 337);
    assert_or_panic(v[1] == 338);
    assert_or_panic(v[2] == 339);
    assert_or_panic(v[3] == 340);
    assert_or_panic(v[4] == 341);
    assert_or_panic(v[5] == 342);
    assert_or_panic(v[6] == 343);
    assert_or_panic(v[7] == 344);
    assert_or_panic(v[8] == 345);
    assert_or_panic(v[9] == 346);
    assert_or_panic(v[10] == 347);
    assert_or_panic(v[11] == 348);
    assert_or_panic(v[12] == 349);
    assert_or_panic(v[13] == 350);
    assert_or_panic(v[14] == 351);
    assert_or_panic(v[15] == 352);
    assert_or_panic(v[16] == 353);
    assert_or_panic(v[17] == 354);
    assert_or_panic(v[18] == 355);
    assert_or_panic(v[19] == 356);
    assert_or_panic(v[20] == 357);
    assert_or_panic(v[21] == 358);
    assert_or_panic(v[22] == 359);
    assert_or_panic(v[23] == 360);
    assert_or_panic(v[24] == 361);
    assert_or_panic(v[25] == 362);
    assert_or_panic(v[26] == 363);
    assert_or_panic(v[27] == 364);
    assert_or_panic(v[28] == 365);
    assert_or_panic(v[29] == 366);
    assert_or_panic(v[30] == 367);
    assert_or_panic(v[31] == 368);
    assert_or_panic(i == 32);
}
Vector_32_f64 c_ret_vector_32_f64(void);
void c_vector_32_f64(Vector_32_f64, size_t);
void c_test_vector_32_f64(void);
static void test_vector_32_f64(void) {
    c_abi_current_test = "@Vector(32, f64)";
    Vector_32_f64 v = c_ret_vector_32_f64();
    assert_or_panic(v[0] == 369);
    assert_or_panic(v[1] == 370);
    assert_or_panic(v[2] == 371);
    assert_or_panic(v[3] == 372);
    assert_or_panic(v[4] == 373);
    assert_or_panic(v[5] == 374);
    assert_or_panic(v[6] == 375);
    assert_or_panic(v[7] == 376);
    assert_or_panic(v[8] == 377);
    assert_or_panic(v[9] == 378);
    assert_or_panic(v[10] == 379);
    assert_or_panic(v[11] == 380);
    assert_or_panic(v[12] == 381);
    assert_or_panic(v[13] == 382);
    assert_or_panic(v[14] == 383);
    assert_or_panic(v[15] == 384);
    assert_or_panic(v[16] == 385);
    assert_or_panic(v[17] == 386);
    assert_or_panic(v[18] == 387);
    assert_or_panic(v[19] == 388);
    assert_or_panic(v[20] == 389);
    assert_or_panic(v[21] == 390);
    assert_or_panic(v[22] == 391);
    assert_or_panic(v[23] == 392);
    assert_or_panic(v[24] == 393);
    assert_or_panic(v[25] == 394);
    assert_or_panic(v[26] == 395);
    assert_or_panic(v[27] == 396);
    assert_or_panic(v[28] == 397);
    assert_or_panic(v[29] == 398);
    assert_or_panic(v[30] == 399);
    assert_or_panic(v[31] == 400);
    c_vector_32_f64((Vector_32_f64){
        401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416,
        417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432,
    }, 32);
    c_test_vector_32_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef double Vector_48_f64 __attribute__((vector_size(48 * sizeof(double))));
Vector_48_f64 zig_ret_vector_48_f64(void) {
    return (Vector_48_f64){
    433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448,
    449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464,
    465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    };
}
void zig_vector_48_f64(Vector_48_f64 v, size_t i) {
    assert_or_panic(v[0] == 481);
    assert_or_panic(v[1] == 482);
    assert_or_panic(v[2] == 483);
    assert_or_panic(v[3] == 484);
    assert_or_panic(v[4] == 485);
    assert_or_panic(v[5] == 486);
    assert_or_panic(v[6] == 487);
    assert_or_panic(v[7] == 488);
    assert_or_panic(v[8] == 489);
    assert_or_panic(v[9] == 490);
    assert_or_panic(v[10] == 491);
    assert_or_panic(v[11] == 492);
    assert_or_panic(v[12] == 493);
    assert_or_panic(v[13] == 494);
    assert_or_panic(v[14] == 495);
    assert_or_panic(v[15] == 496);
    assert_or_panic(v[16] == 497);
    assert_or_panic(v[17] == 498);
    assert_or_panic(v[18] == 499);
    assert_or_panic(v[19] == 500);
    assert_or_panic(v[20] == 501);
    assert_or_panic(v[21] == 502);
    assert_or_panic(v[22] == 503);
    assert_or_panic(v[23] == 504);
    assert_or_panic(v[24] == 505);
    assert_or_panic(v[25] == 506);
    assert_or_panic(v[26] == 507);
    assert_or_panic(v[27] == 508);
    assert_or_panic(v[28] == 509);
    assert_or_panic(v[29] == 510);
    assert_or_panic(v[30] == 511);
    assert_or_panic(v[31] == 512);
    assert_or_panic(v[32] == 513);
    assert_or_panic(v[33] == 514);
    assert_or_panic(v[34] == 515);
    assert_or_panic(v[35] == 516);
    assert_or_panic(v[36] == 517);
    assert_or_panic(v[37] == 518);
    assert_or_panic(v[38] == 519);
    assert_or_panic(v[39] == 520);
    assert_or_panic(v[40] == 521);
    assert_or_panic(v[41] == 522);
    assert_or_panic(v[42] == 523);
    assert_or_panic(v[43] == 524);
    assert_or_panic(v[44] == 525);
    assert_or_panic(v[45] == 526);
    assert_or_panic(v[46] == 527);
    assert_or_panic(v[47] == 528);
    assert_or_panic(i == 48);
}
Vector_48_f64 c_ret_vector_48_f64(void);
void c_vector_48_f64(Vector_48_f64, size_t);
void c_test_vector_48_f64(void);
static void test_vector_48_f64(void) {
    c_abi_current_test = "@Vector(48, f64)";
    Vector_48_f64 v = c_ret_vector_48_f64();
    assert_or_panic(v[0] == 529);
    assert_or_panic(v[1] == 530);
    assert_or_panic(v[2] == 531);
    assert_or_panic(v[3] == 532);
    assert_or_panic(v[4] == 533);
    assert_or_panic(v[5] == 534);
    assert_or_panic(v[6] == 535);
    assert_or_panic(v[7] == 536);
    assert_or_panic(v[8] == 537);
    assert_or_panic(v[9] == 538);
    assert_or_panic(v[10] == 539);
    assert_or_panic(v[11] == 540);
    assert_or_panic(v[12] == 541);
    assert_or_panic(v[13] == 542);
    assert_or_panic(v[14] == 543);
    assert_or_panic(v[15] == 544);
    assert_or_panic(v[16] == 545);
    assert_or_panic(v[17] == 546);
    assert_or_panic(v[18] == 547);
    assert_or_panic(v[19] == 548);
    assert_or_panic(v[20] == 549);
    assert_or_panic(v[21] == 550);
    assert_or_panic(v[22] == 551);
    assert_or_panic(v[23] == 552);
    assert_or_panic(v[24] == 553);
    assert_or_panic(v[25] == 554);
    assert_or_panic(v[26] == 555);
    assert_or_panic(v[27] == 556);
    assert_or_panic(v[28] == 557);
    assert_or_panic(v[29] == 558);
    assert_or_panic(v[30] == 559);
    assert_or_panic(v[31] == 560);
    assert_or_panic(v[32] == 561);
    assert_or_panic(v[33] == 562);
    assert_or_panic(v[34] == 563);
    assert_or_panic(v[35] == 564);
    assert_or_panic(v[36] == 565);
    assert_or_panic(v[37] == 566);
    assert_or_panic(v[38] == 567);
    assert_or_panic(v[39] == 568);
    assert_or_panic(v[40] == 569);
    assert_or_panic(v[41] == 570);
    assert_or_panic(v[42] == 571);
    assert_or_panic(v[43] == 572);
    assert_or_panic(v[44] == 573);
    assert_or_panic(v[45] == 574);
    assert_or_panic(v[46] == 575);
    assert_or_panic(v[47] == 576);
    c_vector_48_f64((Vector_48_f64){
        577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592,
        593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608,
        609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624,
    }, 48);
    c_test_vector_48_f64();
}
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
typedef double Vector_64_f64 __attribute__((vector_size(64 * sizeof(double))));
Vector_64_f64 zig_ret_vector_64_f64(void) {
    return (Vector_64_f64){
    625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640,
    641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656,
    657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672,
    673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688,
    };
}
void zig_vector_64_f64(Vector_64_f64 v, size_t i) {
    assert_or_panic(v[0] == 689);
    assert_or_panic(v[1] == 690);
    assert_or_panic(v[2] == 691);
    assert_or_panic(v[3] == 692);
    assert_or_panic(v[4] == 693);
    assert_or_panic(v[5] == 694);
    assert_or_panic(v[6] == 695);
    assert_or_panic(v[7] == 696);
    assert_or_panic(v[8] == 697);
    assert_or_panic(v[9] == 698);
    assert_or_panic(v[10] == 699);
    assert_or_panic(v[11] == 700);
    assert_or_panic(v[12] == 701);
    assert_or_panic(v[13] == 702);
    assert_or_panic(v[14] == 703);
    assert_or_panic(v[15] == 704);
    assert_or_panic(v[16] == 705);
    assert_or_panic(v[17] == 706);
    assert_or_panic(v[18] == 707);
    assert_or_panic(v[19] == 708);
    assert_or_panic(v[20] == 709);
    assert_or_panic(v[21] == 710);
    assert_or_panic(v[22] == 711);
    assert_or_panic(v[23] == 712);
    assert_or_panic(v[24] == 713);
    assert_or_panic(v[25] == 714);
    assert_or_panic(v[26] == 715);
    assert_or_panic(v[27] == 716);
    assert_or_panic(v[28] == 717);
    assert_or_panic(v[29] == 718);
    assert_or_panic(v[30] == 719);
    assert_or_panic(v[31] == 720);
    assert_or_panic(v[32] == 721);
    assert_or_panic(v[33] == 722);
    assert_or_panic(v[34] == 723);
    assert_or_panic(v[35] == 724);
    assert_or_panic(v[36] == 725);
    assert_or_panic(v[37] == 726);
    assert_or_panic(v[38] == 727);
    assert_or_panic(v[39] == 728);
    assert_or_panic(v[40] == 729);
    assert_or_panic(v[41] == 730);
    assert_or_panic(v[42] == 731);
    assert_or_panic(v[43] == 732);
    assert_or_panic(v[44] == 733);
    assert_or_panic(v[45] == 734);
    assert_or_panic(v[46] == 735);
    assert_or_panic(v[47] == 736);
    assert_or_panic(v[48] == 737);
    assert_or_panic(v[49] == 738);
    assert_or_panic(v[50] == 739);
    assert_or_panic(v[51] == 740);
    assert_or_panic(v[52] == 741);
    assert_or_panic(v[53] == 742);
    assert_or_panic(v[54] == 743);
    assert_or_panic(v[55] == 744);
    assert_or_panic(v[56] == 745);
    assert_or_panic(v[57] == 746);
    assert_or_panic(v[58] == 747);
    assert_or_panic(v[59] == 748);
    assert_or_panic(v[60] == 749);
    assert_or_panic(v[61] == 750);
    assert_or_panic(v[62] == 751);
    assert_or_panic(v[63] == 752);
    assert_or_panic(i == 64);
}
Vector_64_f64 c_ret_vector_64_f64(void);
void c_vector_64_f64(Vector_64_f64, size_t);
void c_test_vector_64_f64(void);
static void test_vector_64_f64(void) {
    c_abi_current_test = "@Vector(64, f64)";
    Vector_64_f64 v = c_ret_vector_64_f64();
    assert_or_panic(v[0] == 753);
    assert_or_panic(v[1] == 754);
    assert_or_panic(v[2] == 755);
    assert_or_panic(v[3] == 756);
    assert_or_panic(v[4] == 757);
    assert_or_panic(v[5] == 758);
    assert_or_panic(v[6] == 759);
    assert_or_panic(v[7] == 760);
    assert_or_panic(v[8] == 761);
    assert_or_panic(v[9] == 762);
    assert_or_panic(v[10] == 763);
    assert_or_panic(v[11] == 764);
    assert_or_panic(v[12] == 765);
    assert_or_panic(v[13] == 766);
    assert_or_panic(v[14] == 767);
    assert_or_panic(v[15] == 768);
    assert_or_panic(v[16] == 769);
    assert_or_panic(v[17] == 770);
    assert_or_panic(v[18] == 771);
    assert_or_panic(v[19] == 772);
    assert_or_panic(v[20] == 773);
    assert_or_panic(v[21] == 774);
    assert_or_panic(v[22] == 775);
    assert_or_panic(v[23] == 776);
    assert_or_panic(v[24] == 777);
    assert_or_panic(v[25] == 778);
    assert_or_panic(v[26] == 779);
    assert_or_panic(v[27] == 780);
    assert_or_panic(v[28] == 781);
    assert_or_panic(v[29] == 782);
    assert_or_panic(v[30] == 783);
    assert_or_panic(v[31] == 784);
    assert_or_panic(v[32] == 785);
    assert_or_panic(v[33] == 786);
    assert_or_panic(v[34] == 787);
    assert_or_panic(v[35] == 788);
    assert_or_panic(v[36] == 789);
    assert_or_panic(v[37] == 790);
    assert_or_panic(v[38] == 791);
    assert_or_panic(v[39] == 792);
    assert_or_panic(v[40] == 793);
    assert_or_panic(v[41] == 794);
    assert_or_panic(v[42] == 795);
    assert_or_panic(v[43] == 796);
    assert_or_panic(v[44] == 797);
    assert_or_panic(v[45] == 798);
    assert_or_panic(v[46] == 799);
    assert_or_panic(v[47] == 800);
    assert_or_panic(v[48] == 801);
    assert_or_panic(v[49] == 802);
    assert_or_panic(v[50] == 803);
    assert_or_panic(v[51] == 804);
    assert_or_panic(v[52] == 805);
    assert_or_panic(v[53] == 806);
    assert_or_panic(v[54] == 807);
    assert_or_panic(v[55] == 808);
    assert_or_panic(v[56] == 809);
    assert_or_panic(v[57] == 810);
    assert_or_panic(v[58] == 811);
    assert_or_panic(v[59] == 812);
    assert_or_panic(v[60] == 813);
    assert_or_panic(v[61] == 814);
    assert_or_panic(v[62] == 815);
    assert_or_panic(v[63] == 816);
    c_vector_64_f64((Vector_64_f64){
        817, 818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 832,
        833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848,
        849, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864,
        865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880,
    }, 64);
    c_test_vector_64_f64();
}
#endif
typedef struct { uint8_t a; } Struct_u8;
Struct_u8 zig_ret_struct_u8(void) {
    return (Struct_u8){ .a = 1 };
}
void zig_struct_u8(Struct_u8 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Struct_u8 c_ret_struct_u8(void);
void c_struct_u8(Struct_u8, size_t);
void c_test_struct_u8(void);
static void test_struct_u8(void) {
    c_abi_current_test = "struct u8";
    Struct_u8 s = c_ret_struct_u8();
    assert_or_panic(s.a == 4);
    c_struct_u8((Struct_u8){ .a = 5 }, 6);
    c_test_struct_u8();
}
typedef struct { uint8_t a; uint8_t b; } Struct_u8_u8;
Struct_u8_u8 zig_ret_struct_u8_u8(void) {
    return (Struct_u8_u8){ .a = 1, .b = 2 };
}
void zig_struct_u8_u8(Struct_u8_u8 s, size_t i) {
    assert_or_panic(s.a == 3);
    assert_or_panic(s.b == 4);
    assert_or_panic(i == 5);
}
Struct_u8_u8 c_ret_struct_u8_u8(void);
void c_struct_u8_u8(Struct_u8_u8, size_t);
void c_test_struct_u8_u8(void);
static void test_struct_u8_u8(void) {
    c_abi_current_test = "struct u8, u8";
    Struct_u8_u8 s = c_ret_struct_u8_u8();
    assert_or_panic(s.a == 6);
    assert_or_panic(s.b == 7);
    c_struct_u8_u8((Struct_u8_u8){ .a = 8, .b = 9 }, 10);
    c_test_struct_u8_u8();
}
typedef struct { uint8_t a; uint8_t b; uint8_t c; } Struct_u8_u8_u8;
Struct_u8_u8_u8 zig_ret_struct_u8_u8_u8(void) {
    return (Struct_u8_u8_u8){ .a = 1, .b = 2, .c = 3 };
}
void zig_struct_u8_u8_u8(Struct_u8_u8_u8 s, size_t i) {
    assert_or_panic(s.a == 4);
    assert_or_panic(s.b == 5);
    assert_or_panic(s.c == 6);
    assert_or_panic(i == 7);
}
Struct_u8_u8_u8 c_ret_struct_u8_u8_u8(void);
void c_struct_u8_u8_u8(Struct_u8_u8_u8, size_t);
void c_test_struct_u8_u8_u8(void);
static void test_struct_u8_u8_u8(void) {
    c_abi_current_test = "struct u8, u8, u8";
    Struct_u8_u8_u8 s = c_ret_struct_u8_u8_u8();
    assert_or_panic(s.a == 8);
    assert_or_panic(s.b == 9);
    assert_or_panic(s.c == 10);
    c_struct_u8_u8_u8((Struct_u8_u8_u8){ .a = 11, .b = 12, .c = 13 }, 14);
    c_test_struct_u8_u8_u8();
}
typedef struct { uint8_t a; uint8_t b; uint8_t c; uint8_t d; } Struct_u8_u8_u8_u8;
Struct_u8_u8_u8_u8 zig_ret_struct_u8_u8_u8_u8(void) {
    return (Struct_u8_u8_u8_u8){ .a = 1, .b = 2, .c = 3, .d = 4 };
}
void zig_struct_u8_u8_u8_u8(Struct_u8_u8_u8_u8 s, size_t i) {
    assert_or_panic(s.a == 5);
    assert_or_panic(s.b == 6);
    assert_or_panic(s.c == 7);
    assert_or_panic(s.d == 8);
    assert_or_panic(i == 9);
}
Struct_u8_u8_u8_u8 c_ret_struct_u8_u8_u8_u8(void);
void c_struct_u8_u8_u8_u8(Struct_u8_u8_u8_u8, size_t);
void c_test_struct_u8_u8_u8_u8(void);
static void test_struct_u8_u8_u8_u8(void) {
    c_abi_current_test = "struct u8, u8, u8, u8";
    Struct_u8_u8_u8_u8 s = c_ret_struct_u8_u8_u8_u8();
    assert_or_panic(s.a == 10);
    assert_or_panic(s.b == 11);
    assert_or_panic(s.c == 12);
    assert_or_panic(s.d == 13);
    c_struct_u8_u8_u8_u8((Struct_u8_u8_u8_u8){ .a = 14, .b = 15, .c = 16, .d = 17 }, 18);
    c_test_struct_u8_u8_u8_u8();
}
typedef struct { uint16_t a; } Struct_u16;
Struct_u16 zig_ret_struct_u16(void) {
    return (Struct_u16){ .a = 1 };
}
void zig_struct_u16(Struct_u16 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Struct_u16 c_ret_struct_u16(void);
void c_struct_u16(Struct_u16, size_t);
void c_test_struct_u16(void);
static void test_struct_u16(void) {
    c_abi_current_test = "struct u16";
    Struct_u16 s = c_ret_struct_u16();
    assert_or_panic(s.a == 4);
    c_struct_u16((Struct_u16){ .a = 5 }, 6);
    c_test_struct_u16();
}
typedef struct { uint16_t a; uint16_t b; } Struct_u16_u16;
Struct_u16_u16 zig_ret_struct_u16_u16(void) {
    return (Struct_u16_u16){ .a = 1, .b = 2 };
}
void zig_struct_u16_u16(Struct_u16_u16 s, size_t i) {
    assert_or_panic(s.a == 3);
    assert_or_panic(s.b == 4);
    assert_or_panic(i == 5);
}
Struct_u16_u16 c_ret_struct_u16_u16(void);
void c_struct_u16_u16(Struct_u16_u16, size_t);
void c_test_struct_u16_u16(void);
static void test_struct_u16_u16(void) {
    c_abi_current_test = "struct u16, u16";
    Struct_u16_u16 s = c_ret_struct_u16_u16();
    assert_or_panic(s.a == 6);
    assert_or_panic(s.b == 7);
    c_struct_u16_u16((Struct_u16_u16){ .a = 8, .b = 9 }, 10);
    c_test_struct_u16_u16();
}
typedef struct { uint16_t a; uint16_t b; uint16_t c; } Struct_u16_u16_u16;
Struct_u16_u16_u16 zig_ret_struct_u16_u16_u16(void) {
    return (Struct_u16_u16_u16){ .a = 1, .b = 2, .c = 3 };
}
void zig_struct_u16_u16_u16(Struct_u16_u16_u16 s, size_t i) {
    assert_or_panic(s.a == 4);
    assert_or_panic(s.b == 5);
    assert_or_panic(s.c == 6);
    assert_or_panic(i == 7);
}
Struct_u16_u16_u16 c_ret_struct_u16_u16_u16(void);
void c_struct_u16_u16_u16(Struct_u16_u16_u16, size_t);
void c_test_struct_u16_u16_u16(void);
static void test_struct_u16_u16_u16(void) {
    c_abi_current_test = "struct u16, u16, u16";
    Struct_u16_u16_u16 s = c_ret_struct_u16_u16_u16();
    assert_or_panic(s.a == 8);
    assert_or_panic(s.b == 9);
    assert_or_panic(s.c == 10);
    c_struct_u16_u16_u16((Struct_u16_u16_u16){ .a = 11, .b = 12, .c = 13 }, 14);
    c_test_struct_u16_u16_u16();
}
typedef struct { uint16_t a; uint16_t b; uint16_t c; uint16_t d; } Struct_u16_u16_u16_u16;
Struct_u16_u16_u16_u16 zig_ret_struct_u16_u16_u16_u16(void) {
    return (Struct_u16_u16_u16_u16){ .a = 1, .b = 2, .c = 3, .d = 4 };
}
void zig_struct_u16_u16_u16_u16(Struct_u16_u16_u16_u16 s, size_t i) {
    assert_or_panic(s.a == 5);
    assert_or_panic(s.b == 6);
    assert_or_panic(s.c == 7);
    assert_or_panic(s.d == 8);
    assert_or_panic(i == 9);
}
Struct_u16_u16_u16_u16 c_ret_struct_u16_u16_u16_u16(void);
void c_struct_u16_u16_u16_u16(Struct_u16_u16_u16_u16, size_t);
void c_test_struct_u16_u16_u16_u16(void);
static void test_struct_u16_u16_u16_u16(void) {
    c_abi_current_test = "struct u16, u16, u16, u16";
    Struct_u16_u16_u16_u16 s = c_ret_struct_u16_u16_u16_u16();
    assert_or_panic(s.a == 10);
    assert_or_panic(s.b == 11);
    assert_or_panic(s.c == 12);
    assert_or_panic(s.d == 13);
    c_struct_u16_u16_u16_u16((Struct_u16_u16_u16_u16){ .a = 14, .b = 15, .c = 16, .d = 17 }, 18);
    c_test_struct_u16_u16_u16_u16();
}
typedef struct { uint32_t a; } Struct_u32;
Struct_u32 zig_ret_struct_u32(void) {
    return (Struct_u32){ .a = 1 };
}
void zig_struct_u32(Struct_u32 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Struct_u32 c_ret_struct_u32(void);
void c_struct_u32(Struct_u32, size_t);
void c_test_struct_u32(void);
static void test_struct_u32(void) {
    c_abi_current_test = "struct u32";
    Struct_u32 s = c_ret_struct_u32();
    assert_or_panic(s.a == 4);
    c_struct_u32((Struct_u32){ .a = 5 }, 6);
    c_test_struct_u32();
}
typedef struct { uint32_t a; uint32_t b; } Struct_u32_u32;
Struct_u32_u32 zig_ret_struct_u32_u32(void) {
    return (Struct_u32_u32){ .a = 1, .b = 2 };
}
void zig_struct_u32_u32(Struct_u32_u32 s, size_t i) {
    assert_or_panic(s.a == 3);
    assert_or_panic(s.b == 4);
    assert_or_panic(i == 5);
}
Struct_u32_u32 c_ret_struct_u32_u32(void);
void c_struct_u32_u32(Struct_u32_u32, size_t);
void c_test_struct_u32_u32(void);
static void test_struct_u32_u32(void) {
    c_abi_current_test = "struct u32, u32";
    Struct_u32_u32 s = c_ret_struct_u32_u32();
    assert_or_panic(s.a == 6);
    assert_or_panic(s.b == 7);
    c_struct_u32_u32((Struct_u32_u32){ .a = 8, .b = 9 }, 10);
    c_test_struct_u32_u32();
}
typedef struct { uint32_t a; uint32_t b; uint32_t c; } Struct_u32_u32_u32;
Struct_u32_u32_u32 zig_ret_struct_u32_u32_u32(void) {
    return (Struct_u32_u32_u32){ .a = 1, .b = 2, .c = 3 };
}
void zig_struct_u32_u32_u32(Struct_u32_u32_u32 s, size_t i) {
    assert_or_panic(s.a == 4);
    assert_or_panic(s.b == 5);
    assert_or_panic(s.c == 6);
    assert_or_panic(i == 7);
}
Struct_u32_u32_u32 c_ret_struct_u32_u32_u32(void);
void c_struct_u32_u32_u32(Struct_u32_u32_u32, size_t);
void c_test_struct_u32_u32_u32(void);
static void test_struct_u32_u32_u32(void) {
    c_abi_current_test = "struct u32, u32, u32";
    Struct_u32_u32_u32 s = c_ret_struct_u32_u32_u32();
    assert_or_panic(s.a == 8);
    assert_or_panic(s.b == 9);
    assert_or_panic(s.c == 10);
    c_struct_u32_u32_u32((Struct_u32_u32_u32){ .a = 11, .b = 12, .c = 13 }, 14);
    c_test_struct_u32_u32_u32();
}
typedef struct { uint32_t a; uint32_t b; uint32_t c; uint32_t d; } Struct_u32_u32_u32_u32;
Struct_u32_u32_u32_u32 zig_ret_struct_u32_u32_u32_u32(void) {
    return (Struct_u32_u32_u32_u32){ .a = 1, .b = 2, .c = 3, .d = 4 };
}
void zig_struct_u32_u32_u32_u32(Struct_u32_u32_u32_u32 s, size_t i) {
    assert_or_panic(s.a == 5);
    assert_or_panic(s.b == 6);
    assert_or_panic(s.c == 7);
    assert_or_panic(s.d == 8);
    assert_or_panic(i == 9);
}
Struct_u32_u32_u32_u32 c_ret_struct_u32_u32_u32_u32(void);
void c_struct_u32_u32_u32_u32(Struct_u32_u32_u32_u32, size_t);
void c_test_struct_u32_u32_u32_u32(void);
static void test_struct_u32_u32_u32_u32(void) {
    c_abi_current_test = "struct u32, u32, u32, u32";
    Struct_u32_u32_u32_u32 s = c_ret_struct_u32_u32_u32_u32();
    assert_or_panic(s.a == 10);
    assert_or_panic(s.b == 11);
    assert_or_panic(s.c == 12);
    assert_or_panic(s.d == 13);
    c_struct_u32_u32_u32_u32((Struct_u32_u32_u32_u32){ .a = 14, .b = 15, .c = 16, .d = 17 }, 18);
    c_test_struct_u32_u32_u32_u32();
}
typedef struct { uint64_t a; } Struct_u64;
Struct_u64 zig_ret_struct_u64(void) {
    return (Struct_u64){ .a = 1 };
}
void zig_struct_u64(Struct_u64 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Struct_u64 c_ret_struct_u64(void);
void c_struct_u64(Struct_u64, size_t);
void c_test_struct_u64(void);
static void test_struct_u64(void) {
    c_abi_current_test = "struct u64";
    Struct_u64 s = c_ret_struct_u64();
    assert_or_panic(s.a == 4);
    c_struct_u64((Struct_u64){ .a = 5 }, 6);
    c_test_struct_u64();
}
typedef struct { uint64_t a; uint64_t b; } Struct_u64_u64;
Struct_u64_u64 zig_ret_struct_u64_u64(void) {
    return (Struct_u64_u64){ .a = 1, .b = 2 };
}
void zig_struct_u64_u64(Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 3);
    assert_or_panic(s.b == 4);
    assert_or_panic(i == 1);
}
void zig_1_struct_u64_u64(size_t ignored0, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 5);
    assert_or_panic(s.b == 6);
    assert_or_panic(i == 2);
}
void zig_2_struct_u64_u64(size_t ignored0, size_t ignored1, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 7);
    assert_or_panic(s.b == 8);
    assert_or_panic(i == 3);
}
void zig_3_struct_u64_u64(size_t ignored0, size_t ignored1, size_t ignored2, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 9);
    assert_or_panic(s.b == 10);
    assert_or_panic(i == 4);
}
void zig_4_struct_u64_u64(size_t ignored0, size_t ignored1, size_t ignored2, size_t ignored3, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 11);
    assert_or_panic(s.b == 12);
    assert_or_panic(i == 5);
}
void zig_5_struct_u64_u64(size_t ignored0, size_t ignored1, size_t ignored2, size_t ignored3, size_t ignored4, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 13);
    assert_or_panic(s.b == 14);
    assert_or_panic(i == 6);
}
void zig_6_struct_u64_u64(size_t ignored0, size_t ignored1, size_t ignored2, size_t ignored3, size_t ignored4, size_t ignored5, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 15);
    assert_or_panic(s.b == 16);
    assert_or_panic(i == 7);
}
void zig_7_struct_u64_u64(size_t ignored0, size_t ignored1, size_t ignored2, size_t ignored3, size_t ignored4, size_t ignored5, size_t ignored6, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 17);
    assert_or_panic(s.b == 18);
    assert_or_panic(i == 8);
}
void zig_8_struct_u64_u64(size_t ignored0, size_t ignored1, size_t ignored2, size_t ignored3, size_t ignored4, size_t ignored5, size_t ignored6, size_t ignored7, Struct_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 19);
    assert_or_panic(s.b == 20);
    assert_or_panic(i == 9);
}
Struct_u64_u64 c_ret_struct_u64_u64(void);
void c_struct_u64_u64(Struct_u64_u64, size_t);
void c_1_struct_u64_u64(size_t, Struct_u64_u64, size_t);
void c_2_struct_u64_u64(size_t, size_t, Struct_u64_u64, size_t);
void c_3_struct_u64_u64(size_t, size_t, size_t, Struct_u64_u64, size_t);
void c_4_struct_u64_u64(size_t, size_t, size_t, size_t, Struct_u64_u64, size_t);
void c_5_struct_u64_u64(size_t, size_t, size_t, size_t, size_t, Struct_u64_u64, size_t);
void c_6_struct_u64_u64(size_t, size_t, size_t, size_t, size_t, size_t, Struct_u64_u64, size_t);
void c_7_struct_u64_u64(size_t, size_t, size_t, size_t, size_t, size_t, size_t, Struct_u64_u64, size_t);
void c_8_struct_u64_u64(size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, Struct_u64_u64, size_t);
void c_test_struct_u64_u64(void);
static void test_struct_u64_u64(void) {
    c_abi_current_test = "struct u64, u64";
    Struct_u64_u64 s = c_ret_struct_u64_u64();
    assert_or_panic(s.a == 21);
    assert_or_panic(s.b == 22);
    c_struct_u64_u64((Struct_u64_u64){ .a = 23, .b = 24 }, 1);
    c_1_struct_u64_u64(0, (Struct_u64_u64){ .a = 25, .b = 26 }, 2);
    c_2_struct_u64_u64(0, 1, (Struct_u64_u64){ .a = 27, .b = 28 }, 3);
    c_3_struct_u64_u64(0, 1, 2, (Struct_u64_u64){ .a = 29, .b = 30 }, 4);
    c_4_struct_u64_u64(0, 1, 2, 3, (Struct_u64_u64){ .a = 31, .b = 32 }, 5);
    c_5_struct_u64_u64(0, 1, 2, 3, 4, (Struct_u64_u64){ .a = 33, .b = 34 }, 6);
    c_6_struct_u64_u64(0, 1, 2, 3, 4, 5, (Struct_u64_u64){ .a = 35, .b = 36 }, 7);
    c_7_struct_u64_u64(0, 1, 2, 3, 4, 5, 6, (Struct_u64_u64){ .a = 37, .b = 38 }, 8);
    c_8_struct_u64_u64(0, 1, 2, 3, 4, 5, 6, 7, (Struct_u64_u64){ .a = 39, .b = 40 }, 9);
    c_test_struct_u64_u64();
}
typedef struct { uint64_t a; uint64_t b; uint64_t c; } Struct_u64_u64_u64;
Struct_u64_u64_u64 zig_ret_struct_u64_u64_u64(void) {
    return (Struct_u64_u64_u64){ .a = 1, .b = 2, .c = 3 };
}
void zig_struct_u64_u64_u64(Struct_u64_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 4);
    assert_or_panic(s.b == 5);
    assert_or_panic(s.c == 6);
    assert_or_panic(i == 7);
}
Struct_u64_u64_u64 c_ret_struct_u64_u64_u64(void);
void c_struct_u64_u64_u64(Struct_u64_u64_u64, size_t);
void c_test_struct_u64_u64_u64(void);
static void test_struct_u64_u64_u64(void) {
    c_abi_current_test = "struct u64, u64, u64";
    Struct_u64_u64_u64 s = c_ret_struct_u64_u64_u64();
    assert_or_panic(s.a == 8);
    assert_or_panic(s.b == 9);
    assert_or_panic(s.c == 10);
    c_struct_u64_u64_u64((Struct_u64_u64_u64){ .a = 11, .b = 12, .c = 13 }, 14);
    c_test_struct_u64_u64_u64();
}
typedef struct { uint64_t a; uint64_t b; uint64_t c; uint64_t d; } Struct_u64_u64_u64_u64;
Struct_u64_u64_u64_u64 zig_ret_struct_u64_u64_u64_u64(void) {
    return (Struct_u64_u64_u64_u64){ .a = 1, .b = 2, .c = 3, .d = 4 };
}
void zig_struct_u64_u64_u64_u64(Struct_u64_u64_u64_u64 s, size_t i) {
    assert_or_panic(s.a == 5);
    assert_or_panic(s.b == 6);
    assert_or_panic(s.c == 7);
    assert_or_panic(s.d == 8);
    assert_or_panic(i == 9);
}
Struct_u64_u64_u64_u64 c_ret_struct_u64_u64_u64_u64(void);
void c_struct_u64_u64_u64_u64(Struct_u64_u64_u64_u64, size_t);
void c_test_struct_u64_u64_u64_u64(void);
static void test_struct_u64_u64_u64_u64(void) {
    c_abi_current_test = "struct u64, u64, u64, u64";
    Struct_u64_u64_u64_u64 s = c_ret_struct_u64_u64_u64_u64();
    assert_or_panic(s.a == 10);
    assert_or_panic(s.b == 11);
    assert_or_panic(s.c == 12);
    assert_or_panic(s.d == 13);
    c_struct_u64_u64_u64_u64((Struct_u64_u64_u64_u64){ .a = 14, .b = 15, .c = 16, .d = 17 }, 18);
    c_test_struct_u64_u64_u64_u64();
}
typedef struct { float a; } Struct_f32;
Struct_f32 zig_ret_struct_f32(void) {
    return (Struct_f32){ .a = 1 };
}
void zig_struct_f32(Struct_f32 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Struct_f32 c_ret_struct_f32(void);
void c_struct_f32(Struct_f32, size_t);
void c_test_struct_f32(void);
static void test_struct_f32(void) {
    c_abi_current_test = "struct f32";
    Struct_f32 s = c_ret_struct_f32();
    assert_or_panic(s.a == 4);
    c_struct_f32((Struct_f32){ .a = 5 }, 6);
    c_test_struct_f32();
}
typedef struct { float a; float b; } Struct_f32_f32;
Struct_f32_f32 zig_ret_struct_f32_f32(void) {
    return (Struct_f32_f32){ .a = 1, .b = 2 };
}
void zig_struct_f32_f32(Struct_f32_f32 s, size_t i) {
    assert_or_panic(s.a == 3);
    assert_or_panic(s.b == 4);
    assert_or_panic(i == 5);
}
Struct_f32_f32 c_ret_struct_f32_f32(void);
void c_struct_f32_f32(Struct_f32_f32, size_t);
void c_test_struct_f32_f32(void);
static void test_struct_f32_f32(void) {
    c_abi_current_test = "struct f32, f32";
    Struct_f32_f32 s = c_ret_struct_f32_f32();
    assert_or_panic(s.a == 6);
    assert_or_panic(s.b == 7);
    c_struct_f32_f32((Struct_f32_f32){ .a = 8, .b = 9 }, 10);
    c_test_struct_f32_f32();
}
typedef struct { float a; float b; float c; } Struct_f32_f32_f32;
Struct_f32_f32_f32 zig_ret_struct_f32_f32_f32(void) {
    return (Struct_f32_f32_f32){ .a = 1, .b = 2, .c = 3 };
}
void zig_struct_f32_f32_f32(Struct_f32_f32_f32 s, size_t i) {
    assert_or_panic(s.a == 4);
    assert_or_panic(s.b == 5);
    assert_or_panic(s.c == 6);
    assert_or_panic(i == 7);
}
Struct_f32_f32_f32 c_ret_struct_f32_f32_f32(void);
void c_struct_f32_f32_f32(Struct_f32_f32_f32, size_t);
void c_test_struct_f32_f32_f32(void);
static void test_struct_f32_f32_f32(void) {
    c_abi_current_test = "struct f32, f32, f32";
    Struct_f32_f32_f32 s = c_ret_struct_f32_f32_f32();
    assert_or_panic(s.a == 8);
    assert_or_panic(s.b == 9);
    assert_or_panic(s.c == 10);
    c_struct_f32_f32_f32((Struct_f32_f32_f32){ .a = 11, .b = 12, .c = 13 }, 14);
    c_test_struct_f32_f32_f32();
}
typedef struct { float a; float b; float c; float d; } Struct_f32_f32_f32_f32;
Struct_f32_f32_f32_f32 zig_ret_struct_f32_f32_f32_f32(void) {
    return (Struct_f32_f32_f32_f32){ .a = 1, .b = 2, .c = 3, .d = 4 };
}
void zig_struct_f32_f32_f32_f32(Struct_f32_f32_f32_f32 s, size_t i) {
    assert_or_panic(s.a == 5);
    assert_or_panic(s.b == 6);
    assert_or_panic(s.c == 7);
    assert_or_panic(s.d == 8);
    assert_or_panic(i == 9);
}
Struct_f32_f32_f32_f32 c_ret_struct_f32_f32_f32_f32(void);
void c_struct_f32_f32_f32_f32(Struct_f32_f32_f32_f32, size_t);
void c_test_struct_f32_f32_f32_f32(void);
static void test_struct_f32_f32_f32_f32(void) {
    c_abi_current_test = "struct f32, f32, f32, f32";
    Struct_f32_f32_f32_f32 s = c_ret_struct_f32_f32_f32_f32();
    assert_or_panic(s.a == 10);
    assert_or_panic(s.b == 11);
    assert_or_panic(s.c == 12);
    assert_or_panic(s.d == 13);
    c_struct_f32_f32_f32_f32((Struct_f32_f32_f32_f32){ .a = 14, .b = 15, .c = 16, .d = 17 }, 18);
    c_test_struct_f32_f32_f32_f32();
}
typedef struct { float a; float b; float c; float d; float e; } Struct_f32_f32_f32_f32_f32;
Struct_f32_f32_f32_f32_f32 zig_ret_struct_f32_f32_f32_f32_f32(void) {
    return (Struct_f32_f32_f32_f32_f32){ .a = 1, .b = 2, .c = 3, .d = 4, .e = 5 };
}
void zig_struct_f32_f32_f32_f32_f32(Struct_f32_f32_f32_f32_f32 s, size_t i) {
    assert_or_panic(s.a == 6);
    assert_or_panic(s.b == 7);
    assert_or_panic(s.c == 8);
    assert_or_panic(s.d == 9);
    assert_or_panic(s.e == 10);
    assert_or_panic(i == 11);
}
Struct_f32_f32_f32_f32_f32 c_ret_struct_f32_f32_f32_f32_f32(void);
void c_struct_f32_f32_f32_f32_f32(Struct_f32_f32_f32_f32_f32, size_t);
void c_test_struct_f32_f32_f32_f32_f32(void);
static void test_struct_f32_f32_f32_f32_f32(void) {
    c_abi_current_test = "struct f32, f32, f32, f32, f32";
    Struct_f32_f32_f32_f32_f32 s = c_ret_struct_f32_f32_f32_f32_f32();
    assert_or_panic(s.a == 12);
    assert_or_panic(s.b == 13);
    assert_or_panic(s.c == 14);
    assert_or_panic(s.d == 15);
    assert_or_panic(s.e == 16);
    c_struct_f32_f32_f32_f32_f32((Struct_f32_f32_f32_f32_f32){ .a = 17, .b = 18, .c = 19, .d = 20, .e = 21 }, 22);
    c_test_struct_f32_f32_f32_f32_f32();
}
typedef struct { float a; } Struct_void_f32;
Struct_void_f32 zig_ret_struct_void_f32(void) {
    return (Struct_void_f32){ .a = 1 };
}
void zig_struct_void_f32(Struct_void_f32 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Struct_void_f32 c_ret_struct_void_f32(void);
void c_struct_void_f32(Struct_void_f32, size_t);
void c_test_struct_void_f32(void);
static void test_struct_void_f32(void) {
    c_abi_current_test = "struct void, f32";
    Struct_void_f32 s = c_ret_struct_void_f32();
    assert_or_panic(s.a == 4);
    c_struct_void_f32((Struct_void_f32){ .a = 5 }, 6);
    c_test_struct_void_f32();
}
typedef struct { float a[1]; } Struct_array_1_f32;
Struct_array_1_f32 zig_ret_struct_array_1_f32(void) {
    return (Struct_array_1_f32){ .a = {1} };
}
void zig_struct_array_1_f32(Struct_array_1_f32 s, size_t i) {
    assert_or_panic(s.a[0] == 2);
    assert_or_panic(i == 3);
}
Struct_array_1_f32 c_ret_struct_array_1_f32(void);
void c_struct_array_1_f32(Struct_array_1_f32, size_t);
void c_test_struct_array_1_f32(void);
static void test_struct_1_f32(void) {
    c_abi_current_test = "struct [1]f32";
#if !(defined(__aarch64__))
    Struct_array_1_f32 s = c_ret_struct_array_1_f32();
    assert_or_panic(s.a[0] == 4);
    c_struct_array_1_f32((Struct_array_1_f32){ .a = {5} }, 6);
    c_test_struct_array_1_f32();
#endif
}
typedef struct { float a[2]; } Struct_array_2_f32;
Struct_array_2_f32 zig_ret_struct_array_2_f32(void) {
    return (Struct_array_2_f32){ .a = { 1, 2 } };
}
void zig_struct_array_2_f32(Struct_array_2_f32 s, size_t i) {
    assert_or_panic(s.a[0] == 3);
    assert_or_panic(s.a[1] == 4);
    assert_or_panic(i == 5);
}
Struct_array_2_f32 c_ret_struct_array_2_f32(void);
void c_struct_array_2_f32(Struct_array_2_f32, size_t);
void c_test_struct_array_2_f32(void);
static void test_struct_2_f32(void) {
    c_abi_current_test = "struct [2]f32";
#if !(defined(__aarch64__))
    Struct_array_2_f32 s = c_ret_struct_array_2_f32();
    assert_or_panic(s.a[0] == 6);
    assert_or_panic(s.a[1] == 7);
    c_struct_array_2_f32((Struct_array_2_f32){ .a = { 8, 9 } }, 10);
    c_test_struct_array_2_f32();
#endif
}
typedef struct { float a[3]; } Struct_array_3_f32;
Struct_array_3_f32 zig_ret_struct_array_3_f32(void) {
    return (Struct_array_3_f32){ .a = { 1, 2, 3 } };
}
void zig_struct_array_3_f32(Struct_array_3_f32 s, size_t i) {
    assert_or_panic(s.a[0] == 4);
    assert_or_panic(s.a[1] == 5);
    assert_or_panic(s.a[2] == 6);
    assert_or_panic(i == 7);
}
Struct_array_3_f32 c_ret_struct_array_3_f32(void);
void c_struct_array_3_f32(Struct_array_3_f32, size_t);
void c_test_struct_array_3_f32(void);
static void test_struct_3_f32(void) {
    c_abi_current_test = "struct [3]f32";
#if !(defined(__aarch64__))
    Struct_array_3_f32 s = c_ret_struct_array_3_f32();
    assert_or_panic(s.a[0] == 8);
    assert_or_panic(s.a[1] == 9);
    assert_or_panic(s.a[2] == 10);
    c_struct_array_3_f32((Struct_array_3_f32){ .a = { 11, 12, 13 } }, 14);
    c_test_struct_array_3_f32();
#endif
}
typedef struct { float a[4]; } Struct_array_4_f32;
Struct_array_4_f32 zig_ret_struct_array_4_f32(void) {
    return (Struct_array_4_f32){ .a = { 1, 2, 3, 4 } };
}
void zig_struct_array_4_f32(Struct_array_4_f32 s, size_t i) {
    assert_or_panic(s.a[0] == 5);
    assert_or_panic(s.a[1] == 6);
    assert_or_panic(s.a[2] == 7);
    assert_or_panic(s.a[3] == 8);
    assert_or_panic(i == 9);
}
Struct_array_4_f32 c_ret_struct_array_4_f32(void);
void c_struct_array_4_f32(Struct_array_4_f32, size_t);
void c_test_struct_array_4_f32(void);
static void test_struct_4_f32(void) {
    c_abi_current_test = "struct [4]f32";
#if !(defined(__aarch64__))
    Struct_array_4_f32 s = c_ret_struct_array_4_f32();
    assert_or_panic(s.a[0] == 10);
    assert_or_panic(s.a[1] == 11);
    assert_or_panic(s.a[2] == 12);
    assert_or_panic(s.a[3] == 13);
    c_struct_array_4_f32((Struct_array_4_f32){ .a = { 14, 15, 16, 17 } }, 18);
    c_test_struct_array_4_f32();
#endif
}
typedef struct { float a[5]; } Struct_array_5_f32;
Struct_array_5_f32 zig_ret_struct_array_5_f32(void) {
    return (Struct_array_5_f32){ .a = { 1, 2, 3, 4, 5 } };
}
void zig_struct_array_5_f32(Struct_array_5_f32 s, size_t i) {
    assert_or_panic(s.a[0] == 6);
    assert_or_panic(s.a[1] == 7);
    assert_or_panic(s.a[2] == 8);
    assert_or_panic(s.a[3] == 9);
    assert_or_panic(s.a[4] == 10);
    assert_or_panic(i == 11);
}
Struct_array_5_f32 c_ret_struct_array_5_f32(void);
void c_struct_array_5_f32(Struct_array_5_f32, size_t);
void c_test_struct_array_5_f32(void);
static void test_struct_5_f32(void) {
    c_abi_current_test = "struct [5]f32";
    Struct_array_5_f32 s = c_ret_struct_array_5_f32();
    assert_or_panic(s.a[0] == 12);
    assert_or_panic(s.a[1] == 13);
    assert_or_panic(s.a[2] == 14);
    assert_or_panic(s.a[3] == 15);
    assert_or_panic(s.a[4] == 16);
    c_struct_array_5_f32((Struct_array_5_f32){ .a = { 17, 18, 19, 20, 21 } }, 22);
    c_test_struct_array_5_f32();
}
typedef struct { float a[1]; } Struct_array_0_sentinel_f32;
Struct_array_0_sentinel_f32 zig_ret_struct_array_0_sentinel_f32(void) {
    return (Struct_array_0_sentinel_f32){ .a = {0x1e1} };
}
void zig_struct_array_0_sentinel_f32(Struct_array_0_sentinel_f32 s, size_t i) {
    volatile size_t sentinel_index = 0;
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    assert_or_panic(i == 1);
}
Struct_array_0_sentinel_f32 c_ret_struct_array_0_sentinel_f32(void);
void c_struct_array_0_sentinel_f32(Struct_array_0_sentinel_f32, size_t);
void c_test_struct_array_0_sentinel_f32(void);
static void test_struct_0_sentinel_f32(void) {
    c_abi_current_test = "struct [0:sentinel]f32";
#if !(defined(__aarch64__))
    volatile size_t sentinel_index = 0;
    Struct_array_0_sentinel_f32 s = c_ret_struct_array_0_sentinel_f32();
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    c_struct_array_0_sentinel_f32((Struct_array_0_sentinel_f32){ .a = {0x1e1} }, 2);
    c_test_struct_array_0_sentinel_f32();
#endif
}
typedef struct { float a[2]; } Struct_array_1_sentinel_f32;
Struct_array_1_sentinel_f32 zig_ret_struct_array_1_sentinel_f32(void) {
    return (Struct_array_1_sentinel_f32){ .a = {1, 0x1e1} };
}
void zig_struct_array_1_sentinel_f32(Struct_array_1_sentinel_f32 s, size_t i) {
    volatile size_t sentinel_index = 1;
    assert_or_panic(s.a[0] == 2);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    assert_or_panic(i == 3);
}
Struct_array_1_sentinel_f32 c_ret_struct_array_1_sentinel_f32(void);
void c_struct_array_1_sentinel_f32(Struct_array_1_sentinel_f32, size_t);
void c_test_struct_array_1_sentinel_f32(void);
static void test_struct_1_sentinel_f32(void) {
    c_abi_current_test = "struct [1:sentinel]f32";
#if !(defined(__aarch64__))
    volatile size_t sentinel_index = 1;
    Struct_array_1_sentinel_f32 s = c_ret_struct_array_1_sentinel_f32();
    assert_or_panic(s.a[0] == 4);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    c_struct_array_1_sentinel_f32((Struct_array_1_sentinel_f32){ .a = {5, 0x1e1} }, 6);
    c_test_struct_array_1_sentinel_f32();
#endif
}
typedef struct { float a[3]; } Struct_array_2_sentinel_f32;
Struct_array_2_sentinel_f32 zig_ret_struct_array_2_sentinel_f32(void) {
    return (Struct_array_2_sentinel_f32){ .a = {1, 2, 0x1e1} };
}
void zig_struct_array_2_sentinel_f32(Struct_array_2_sentinel_f32 s, size_t i) {
    volatile size_t sentinel_index = 2;
    assert_or_panic(s.a[0] == 3);
    assert_or_panic(s.a[1] == 4);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    assert_or_panic(i == 5);
}
Struct_array_2_sentinel_f32 c_ret_struct_array_2_sentinel_f32(void);
void c_struct_array_2_sentinel_f32(Struct_array_2_sentinel_f32, size_t);
void c_test_struct_array_2_sentinel_f32(void);
static void test_struct_2_sentinel_f32(void) {
    c_abi_current_test = "struct [2:sentinel]f32";
#if !(defined(__aarch64__))
    volatile size_t sentinel_index = 2;
    Struct_array_2_sentinel_f32 s = c_ret_struct_array_2_sentinel_f32();
    assert_or_panic(s.a[0] == 6);
    assert_or_panic(s.a[1] == 7);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    c_struct_array_2_sentinel_f32((Struct_array_2_sentinel_f32){ .a = {8, 9, 0x1e1} }, 10);
    c_test_struct_array_2_sentinel_f32();
#endif
}
typedef struct { float a[4]; } Struct_array_3_sentinel_f32;
Struct_array_3_sentinel_f32 zig_ret_struct_array_3_sentinel_f32(void) {
    return (Struct_array_3_sentinel_f32){ .a = {1, 2, 3, 0x1e1} };
}
void zig_struct_array_3_sentinel_f32(Struct_array_3_sentinel_f32 s, size_t i) {
    volatile size_t sentinel_index = 3;
    assert_or_panic(s.a[0] == 4);
    assert_or_panic(s.a[1] == 5);
    assert_or_panic(s.a[2] == 6);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    assert_or_panic(i == 7);
}
Struct_array_3_sentinel_f32 c_ret_struct_array_3_sentinel_f32(void);
void c_struct_array_3_sentinel_f32(Struct_array_3_sentinel_f32, size_t);
void c_test_struct_array_3_sentinel_f32(void);
static void test_struct_3_sentinel_f32(void) {
    c_abi_current_test = "struct [3:sentinel]f32";
#if !(defined(__aarch64__))
    volatile size_t sentinel_index = 3;
    Struct_array_3_sentinel_f32 s = c_ret_struct_array_3_sentinel_f32();
    assert_or_panic(s.a[0] == 8);
    assert_or_panic(s.a[1] == 9);
    assert_or_panic(s.a[2] == 10);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    c_struct_array_3_sentinel_f32((Struct_array_3_sentinel_f32){ .a = {11, 12, 13, 0x1e1} }, 14);
    c_test_struct_array_3_sentinel_f32();
#endif
}
typedef struct { float a[5]; } Struct_array_4_sentinel_f32;
Struct_array_4_sentinel_f32 zig_ret_struct_array_4_sentinel_f32(void) {
    return (Struct_array_4_sentinel_f32){ .a = {1, 2, 3, 4, 0x1e1} };
}
void zig_struct_array_4_sentinel_f32(Struct_array_4_sentinel_f32 s, size_t i) {
    volatile size_t sentinel_index = 4;
    assert_or_panic(s.a[0] == 5);
    assert_or_panic(s.a[1] == 6);
    assert_or_panic(s.a[2] == 7);
    assert_or_panic(s.a[3] == 8);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    assert_or_panic(i == 9);
}
Struct_array_4_sentinel_f32 c_ret_struct_array_4_sentinel_f32(void);
void c_struct_array_4_sentinel_f32(Struct_array_4_sentinel_f32, size_t);
void c_test_struct_array_4_sentinel_f32(void);
static void test_struct_4_sentinel_f32(void) {
    c_abi_current_test = "struct [4:sentinel]f32";
    volatile size_t sentinel_index = 4;
    Struct_array_4_sentinel_f32 s = c_ret_struct_array_4_sentinel_f32();
    assert_or_panic(s.a[0] == 10);
    assert_or_panic(s.a[1] == 11);
    assert_or_panic(s.a[2] == 12);
    assert_or_panic(s.a[3] == 13);
    assert_or_panic(s.a[sentinel_index] == 0x1e1);
    c_struct_array_4_sentinel_f32((Struct_array_4_sentinel_f32){ .a = {14, 15, 16, 17, 0x1e1} }, 18);
    c_test_struct_array_4_sentinel_f32();
}
typedef struct { alignas(8) float a; } Struct_f32a8;
Struct_f32a8 zig_ret_struct_f32a8(void) {
    return (Struct_f32a8){ .a = 1.25 };
}
void zig_struct_f32a8(Struct_f32a8 s, float f) {
    assert_or_panic(s.a == 2.75);
    assert_or_panic(f == 3.5);
}
Struct_f32a8 c_ret_struct_f32a8(void);
void c_struct_f32a8(Struct_f32a8, float);
void c_test_struct_f32a8(void);
static void test_struct_f32_align_8(void) {
    c_abi_current_test = "struct f32 align(8)";
    Struct_f32a8 s = c_ret_struct_f32a8();
    assert_or_panic(s.a == 4.125);
    c_struct_f32a8((Struct_f32a8){ .a = 5.375 }, 6.5);
    c_test_struct_f32a8();
}
typedef struct { alignas(8) float a; alignas(8) float b; } Struct_f32a8_f32a8;
Struct_f32a8_f32a8 zig_ret_struct_f32a8_f32a8(void) {
    return (Struct_f32a8_f32a8){ .a = 1.25, .b = 2.75 };
}
void zig_struct_f32a8_f32a8(Struct_f32a8_f32a8 s, float f) {
    assert_or_panic(s.a == 3.125);
    assert_or_panic(s.b == 4.375);
    assert_or_panic(f == 5.5);
}
Struct_f32a8_f32a8 c_ret_struct_f32a8_f32a8(void);
void c_struct_f32a8_f32a8(Struct_f32a8_f32a8, float);
void c_test_struct_f32a8_f32a8(void);
static void test_struct_f32_align_8_f32_align_8(void) {
    c_abi_current_test = "struct f32 align(8), f32 align(8)";
    Struct_f32a8_f32a8 s = c_ret_struct_f32a8_f32a8();
    assert_or_panic(s.a == 6.625);
    assert_or_panic(s.b == 7.875);
    c_struct_f32a8_f32a8((Struct_f32a8_f32a8){ .a = 8.0625, .b = 9.1875 }, 10.5);
    c_test_struct_f32a8_f32a8();
}
typedef struct { struct { float b; float c; } a; float d; } Struct_f32f32_f32;
Struct_f32f32_f32 zig_ret_struct_f32f32_f32(void) {
    return (Struct_f32f32_f32){ .a = { .b = 1.0, .c = 2.0 }, .d = 3.0 };
}
void zig_struct_f32f32_f32(Struct_f32f32_f32 s) {
    assert_or_panic(s.a.b == 1.0);
    assert_or_panic(s.a.c == 2.0);
    assert_or_panic(s.d == 3.0);
}
Struct_f32f32_f32 c_ret_struct_f32f32_f32(void);
void c_struct_f32f32_f32(Struct_f32f32_f32);
void c_test_struct_f32f32_f32(void);
static void test_struct_f32_f32_f32_2(void) {
    c_abi_current_test = "struct {f32, f32}, f32";
    Struct_f32f32_f32 s = c_ret_struct_f32f32_f32();
    assert_or_panic(s.a.b == 1.0);
    assert_or_panic(s.a.c == 2.0);
    assert_or_panic(s.d == 3.0);
    c_struct_f32f32_f32((Struct_f32f32_f32){ .a = { .b = 1.0, .c = 2.0 }, .d = 3.0 });
    c_test_struct_f32f32_f32();
}
typedef struct { float a; struct { float c; float d; } b; } Struct_f32_f32f32;
Struct_f32_f32f32 zig_ret_struct_f32_f32f32(void) {
    return (Struct_f32_f32f32){ .a = 1.0, .b = { .c = 2.0, .d = 3.0 } };
}
void zig_struct_f32_f32f32(Struct_f32_f32f32 s) {
    assert_or_panic(s.a == 1.0);
    assert_or_panic(s.b.c == 2.0);
    assert_or_panic(s.b.d == 3.0);
}
Struct_f32_f32f32 c_ret_struct_f32_f32f32(void);
void c_struct_f32_f32f32(Struct_f32_f32f32);
void c_test_struct_f32_f32f32(void);
static void test_struct_f32_f32_f32_3(void) {
    c_abi_current_test = "struct f32, {f32, f32}";
    Struct_f32_f32f32 s = c_ret_struct_f32_f32f32();
    assert_or_panic(s.a == 1.0);
    assert_or_panic(s.b.c == 2.0);
    assert_or_panic(s.b.d == 3.0);
    c_struct_f32_f32f32((Struct_f32_f32f32){ .a = 1.0, .b = { .c = 2.0, .d = 3.0 } });
    c_test_struct_f32_f32f32();
}
typedef struct { double a; } Struct_f64;
Struct_f64 zig_ret_struct_f64(void) {
    return (Struct_f64){ .a = 1 };
}
void zig_struct_f64(Struct_f64 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Struct_f64 c_ret_struct_f64(void);
void c_struct_f64(Struct_f64, size_t);
void c_test_struct_f64(void);
static void test_struct_f64(void) {
    c_abi_current_test = "struct f64";
    Struct_f64 s = c_ret_struct_f64();
    assert_or_panic(s.a == 4);
    c_struct_f64((Struct_f64){ .a = 5 }, 6);
    c_test_struct_f64();
}
typedef struct { double a; double b; } Struct_f64_f64;
Struct_f64_f64 zig_ret_struct_f64_f64(void) {
    return (Struct_f64_f64){ .a = 1, .b = 2 };
}
void zig_struct_f64_f64(Struct_f64_f64 s, size_t i) {
    assert_or_panic(s.a == 3);
    assert_or_panic(s.b == 4);
    assert_or_panic(i == 5);
}
Struct_f64_f64 c_ret_struct_f64_f64(void);
void c_struct_f64_f64(Struct_f64_f64, size_t);
void c_test_struct_f64_f64(void);
static void test_struct_f64_f64(void) {
    c_abi_current_test = "struct f64, f64";
    Struct_f64_f64 s = c_ret_struct_f64_f64();
    assert_or_panic(s.a == 6);
    assert_or_panic(s.b == 7);
    c_struct_f64_f64((Struct_f64_f64){ .a = 8, .b = 9 }, 10);
    c_test_struct_f64_f64();
}
typedef struct { double a; double b; double c; } Struct_f64_f64_f64;
Struct_f64_f64_f64 zig_ret_struct_f64_f64_f64(void) {
    return (Struct_f64_f64_f64){ .a = 1, .b = 2, .c = 3 };
}
void zig_struct_f64_f64_f64(Struct_f64_f64_f64 s, size_t i) {
    assert_or_panic(s.a == 4);
    assert_or_panic(s.b == 5);
    assert_or_panic(s.c == 6);
    assert_or_panic(i == 7);
}
Struct_f64_f64_f64 c_ret_struct_f64_f64_f64(void);
void c_struct_f64_f64_f64(Struct_f64_f64_f64, size_t);
void c_test_struct_f64_f64_f64(void);
static void test_struct_f64_f64_f64(void) {
    c_abi_current_test = "struct f64, f64, f64";
    Struct_f64_f64_f64 s = c_ret_struct_f64_f64_f64();
    assert_or_panic(s.a == 8);
    assert_or_panic(s.b == 9);
    assert_or_panic(s.c == 10);
    c_struct_f64_f64_f64((Struct_f64_f64_f64){ .a = 11, .b = 12, .c = 13 }, 14);
    c_test_struct_f64_f64_f64();
}
typedef struct { double a; double b; double c; double d; } Struct_f64_f64_f64_f64;
Struct_f64_f64_f64_f64 zig_ret_struct_f64_f64_f64_f64(void) {
    return (Struct_f64_f64_f64_f64){ .a = 1, .b = 2, .c = 3, .d = 4 };
}
void zig_struct_f64_f64_f64_f64(Struct_f64_f64_f64_f64 s, size_t i) {
    assert_or_panic(s.a == 5);
    assert_or_panic(s.b == 6);
    assert_or_panic(s.c == 7);
    assert_or_panic(s.d == 8);
    assert_or_panic(i == 9);
}
Struct_f64_f64_f64_f64 c_ret_struct_f64_f64_f64_f64(void);
void c_struct_f64_f64_f64_f64(Struct_f64_f64_f64_f64, size_t);
void c_test_struct_f64_f64_f64_f64(void);
static void test_struct_f64_f64_f64_f64(void) {
    c_abi_current_test = "struct f64, f64, f64, f64";
    Struct_f64_f64_f64_f64 s = c_ret_struct_f64_f64_f64_f64();
    assert_or_panic(s.a == 10);
    assert_or_panic(s.b == 11);
    assert_or_panic(s.c == 12);
    assert_or_panic(s.d == 13);
    c_struct_f64_f64_f64_f64((Struct_f64_f64_f64_f64){ .a = 14, .b = 15, .c = 16, .d = 17 }, 18);
    c_test_struct_f64_f64_f64_f64();
}
typedef struct { double a; double b; double c; double d; double e; } Struct_f64_f64_f64_f64_f64;
Struct_f64_f64_f64_f64_f64 zig_ret_struct_f64_f64_f64_f64_f64(void) {
    return (Struct_f64_f64_f64_f64_f64){ .a = 1, .b = 2, .c = 3, .d = 4, .e = 5 };
}
void zig_struct_f64_f64_f64_f64_f64(Struct_f64_f64_f64_f64_f64 s, size_t i) {
    assert_or_panic(s.a == 6);
    assert_or_panic(s.b == 7);
    assert_or_panic(s.c == 8);
    assert_or_panic(s.d == 9);
    assert_or_panic(s.e == 10);
    assert_or_panic(i == 11);
}
Struct_f64_f64_f64_f64_f64 c_ret_struct_f64_f64_f64_f64_f64(void);
void c_struct_f64_f64_f64_f64_f64(Struct_f64_f64_f64_f64_f64, size_t);
void c_test_struct_f64_f64_f64_f64_f64(void);
static void test_struct_f64_f64_f64_f64_f64(void) {
    c_abi_current_test = "struct f64, f64, f64, f64, f64";
    Struct_f64_f64_f64_f64_f64 s = c_ret_struct_f64_f64_f64_f64_f64();
    assert_or_panic(s.a == 12);
    assert_or_panic(s.b == 13);
    assert_or_panic(s.c == 14);
    assert_or_panic(s.d == 15);
    assert_or_panic(s.e == 16);
    c_struct_f64_f64_f64_f64_f64((Struct_f64_f64_f64_f64_f64){ .a = 17, .b = 18, .c = 19, .d = 20, .e = 21 }, 22);
    c_test_struct_f64_f64_f64_f64_f64();
}
typedef struct { double a[1]; } Struct_array_1_f64;
Struct_array_1_f64 zig_ret_struct_array_1_f64(void) {
    return (Struct_array_1_f64){ .a = {1} };
}
void zig_struct_array_1_f64(Struct_array_1_f64 s, size_t i) {
    assert_or_panic(s.a[0] == 2);
    assert_or_panic(i == 3);
}
Struct_array_1_f64 c_ret_struct_array_1_f64(void);
void c_struct_array_1_f64(Struct_array_1_f64, size_t);
void c_test_struct_array_1_f64(void);
static void test_struct_1_f64(void) {
    c_abi_current_test = "struct [1]f64";
#if !(defined(__aarch64__))
    Struct_array_1_f64 s = c_ret_struct_array_1_f64();
    assert_or_panic(s.a[0] == 4);
    c_struct_array_1_f64((Struct_array_1_f64){ .a = {5} }, 6);
    c_test_struct_array_1_f64();
#endif
}
typedef struct { double a[2]; } Struct_array_2_f64;
Struct_array_2_f64 zig_ret_struct_array_2_f64(void) {
    return (Struct_array_2_f64){ .a = { 1, 2 } };
}
void zig_struct_array_2_f64(Struct_array_2_f64 s, size_t i) {
    assert_or_panic(s.a[0] == 3);
    assert_or_panic(s.a[1] == 4);
    assert_or_panic(i == 5);
}
Struct_array_2_f64 c_ret_struct_array_2_f64(void);
void c_struct_array_2_f64(Struct_array_2_f64, size_t);
void c_test_struct_array_2_f64(void);
static void test_struct_2_f64(void) {
    c_abi_current_test = "struct [2]f64";
#if !(defined(__aarch64__))
    Struct_array_2_f64 s = c_ret_struct_array_2_f64();
    assert_or_panic(s.a[0] == 6);
    assert_or_panic(s.a[1] == 7);
    c_struct_array_2_f64((Struct_array_2_f64){ .a = { 8, 9 } }, 10);
    c_test_struct_array_2_f64();
#endif
}
typedef struct { double a[3]; } Struct_array_3_f64;
Struct_array_3_f64 zig_ret_struct_array_3_f64(void) {
    return (Struct_array_3_f64){ .a = { 1, 2, 3 } };
}
void zig_struct_array_3_f64(Struct_array_3_f64 s, size_t i) {
    assert_or_panic(s.a[0] == 4);
    assert_or_panic(s.a[1] == 5);
    assert_or_panic(s.a[2] == 6);
    assert_or_panic(i == 7);
}
Struct_array_3_f64 c_ret_struct_array_3_f64(void);
void c_struct_array_3_f64(Struct_array_3_f64, size_t);
void c_test_struct_array_3_f64(void);
static void test_struct_3_f64(void) {
    c_abi_current_test = "struct [3]f64";
#if !(defined(__aarch64__))
    Struct_array_3_f64 s = c_ret_struct_array_3_f64();
    assert_or_panic(s.a[0] == 8);
    assert_or_panic(s.a[1] == 9);
    assert_or_panic(s.a[2] == 10);
    c_struct_array_3_f64((Struct_array_3_f64){ .a = { 11, 12, 13 } }, 14);
    c_test_struct_array_3_f64();
#endif
}
typedef struct { double a[4]; } Struct_array_4_f64;
Struct_array_4_f64 zig_ret_struct_array_4_f64(void) {
    return (Struct_array_4_f64){ .a = { 1, 2, 3, 4 } };
}
void zig_struct_array_4_f64(Struct_array_4_f64 s, size_t i) {
    assert_or_panic(s.a[0] == 5);
    assert_or_panic(s.a[1] == 6);
    assert_or_panic(s.a[2] == 7);
    assert_or_panic(s.a[3] == 8);
    assert_or_panic(i == 9);
}
Struct_array_4_f64 c_ret_struct_array_4_f64(void);
void c_struct_array_4_f64(Struct_array_4_f64, size_t);
void c_test_struct_array_4_f64(void);
static void test_struct_4_f64(void) {
    c_abi_current_test = "struct [4]f64";
#if !(defined(__aarch64__))
    Struct_array_4_f64 s = c_ret_struct_array_4_f64();
    assert_or_panic(s.a[0] == 10);
    assert_or_panic(s.a[1] == 11);
    assert_or_panic(s.a[2] == 12);
    assert_or_panic(s.a[3] == 13);
    c_struct_array_4_f64((Struct_array_4_f64){ .a = { 14, 15, 16, 17 } }, 18);
    c_test_struct_array_4_f64();
#endif
}
typedef struct { double a[5]; } Struct_array_5_f64;
Struct_array_5_f64 zig_ret_struct_array_5_f64(void) {
    return (Struct_array_5_f64){ .a = { 1, 2, 3, 4, 5 } };
}
void zig_struct_array_5_f64(Struct_array_5_f64 s, size_t i) {
    assert_or_panic(s.a[0] == 6);
    assert_or_panic(s.a[1] == 7);
    assert_or_panic(s.a[2] == 8);
    assert_or_panic(s.a[3] == 9);
    assert_or_panic(s.a[4] == 10);
    assert_or_panic(i == 11);
}
Struct_array_5_f64 c_ret_struct_array_5_f64(void);
void c_struct_array_5_f64(Struct_array_5_f64, size_t);
void c_test_struct_array_5_f64(void);
static void test_struct_5_f64(void) {
    c_abi_current_test = "struct [5]f64";
    Struct_array_5_f64 s = c_ret_struct_array_5_f64();
    assert_or_panic(s.a[0] == 12);
    assert_or_panic(s.a[1] == 13);
    assert_or_panic(s.a[2] == 14);
    assert_or_panic(s.a[3] == 15);
    assert_or_panic(s.a[4] == 16);
    c_struct_array_5_f64((Struct_array_5_f64){ .a = { 17, 18, 19, 20, 21 } }, 22);
    c_test_struct_array_5_f64();
}
typedef union { double a; } Union_f64;
Union_f64 zig_ret_union_f64(void) {
    return (Union_f64){ .a = 1 };
}
void zig_union_f64(Union_f64 s, size_t i) {
    assert_or_panic(s.a == 2);
    assert_or_panic(i == 3);
}
Union_f64 c_ret_union_f64(void);
void c_union_f64(Union_f64, size_t);
void c_test_union_f64(void);
static void test_union_f64(void) {
    c_abi_current_test = "union f64";
    Union_f64 s = c_ret_union_f64();
    assert_or_panic(s.a == 4);
    c_union_f64((Union_f64){ .a = 5 }, 6);
    c_test_union_f64();
}
typedef struct { uint32_t a; union { struct { uint32_t d; uint32_t e; } c; } b; } Struct_u32_Union_u32_u32u32;
Struct_u32_Union_u32_u32u32 zig_ret_struct_u32_union_u32_u32u32(void) {
    return (Struct_u32_Union_u32_u32u32){ .a = 1, .b = { .c = { .d = 2, .e = 3 } } };
}
void zig_struct_u32_union_u32_u32u32(Struct_u32_Union_u32_u32u32 s) {
    assert_or_panic(s.a == 1);
    assert_or_panic(s.b.c.d == 2);
    assert_or_panic(s.b.c.e == 3);
}
Struct_u32_Union_u32_u32u32 c_ret_struct_u32_union_u32_u32u32(void);
void c_struct_u32_union_u32_u32u32(Struct_u32_Union_u32_u32u32);
void c_test_struct_u32_union_u32_u32u32(void);
static void test_struct_u32_union_u32_struct_u32_u32(void) {
    c_abi_current_test = "struct{u32,union{u32,struct{u32,u32}}}";
    Struct_u32_Union_u32_u32u32 s = c_ret_struct_u32_union_u32_u32u32();
    assert_or_panic(s.a == 1);
    assert_or_panic(s.b.c.d == 2);
    assert_or_panic(s.b.c.e == 3);
    c_struct_u32_union_u32_u32u32((Struct_u32_Union_u32_u32u32){ .a = 1, .b = { .c = { .d = 2, .e = 3 } } });
    c_test_struct_u32_union_u32_u32u32();
}

void c_abi_run_generated_tests(void) {
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_TINY_VECTORS)
    test_vector_1_u8();
    test_vector_2_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_TINY_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_3_u8();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_4_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_6_u8();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_8_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_12_u8();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_16_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_24_u8();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_32_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_48_u8();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_64_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_96_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_128_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_192_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_256_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_384_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_512_u8();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_TINY_VECTORS)
    test_vector_1_u16();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_2_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_3_u16();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_4_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_6_u16();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_8_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_12_u16();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_16_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_24_u16();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_32_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_48_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_64_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_96_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_128_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_192_u16();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_256_u16();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_1_u32();
    test_vector_2_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_3_u32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_4_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_6_u32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_8_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_12_u32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_16_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_24_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_32_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_48_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_64_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_96_u32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_128_u32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_1_u64();
    test_vector_2_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_3_u64();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_4_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_6_u64();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_8_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_12_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_16_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_24_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_32_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_48_u64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_64_u64();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_1_f32();
    test_vector_2_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_3_f32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_4_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_6_f32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_8_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_12_f32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_16_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_24_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_32_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_48_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_64_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_96_f32();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_128_f32();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_1_f64();
    test_vector_2_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_3_f64();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_4_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS)
    test_vector_6_f64();
#endif
#if !defined(ZIG_NO_VECTORS)
    test_vector_8_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_12_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_16_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_24_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_32_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_NON_POW2_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_48_f64();
#endif
#if !defined(ZIG_NO_VECTORS) && !defined(ZIG_NO_WIDE_VECTORS)
    test_vector_64_f64();
#endif
    test_struct_u8();
    test_struct_u8_u8();
    test_struct_u8_u8_u8();
    test_struct_u8_u8_u8_u8();
    test_struct_u16();
    test_struct_u16_u16();
    test_struct_u16_u16_u16();
    test_struct_u16_u16_u16_u16();
    test_struct_u32();
    test_struct_u32_u32();
    test_struct_u32_u32_u32();
    test_struct_u32_u32_u32_u32();
    test_struct_u64();
    test_struct_u64_u64();
    test_struct_u64_u64_u64();
    test_struct_u64_u64_u64_u64();
    test_struct_f32();
    test_struct_f32_f32();
    test_struct_f32_f32_f32();
    test_struct_f32_f32_f32_f32();
    test_struct_f32_f32_f32_f32_f32();
    test_struct_void_f32();
    test_struct_1_f32();
    test_struct_2_f32();
    test_struct_3_f32();
    test_struct_4_f32();
    test_struct_5_f32();
    test_struct_0_sentinel_f32();
    test_struct_1_sentinel_f32();
    test_struct_2_sentinel_f32();
    test_struct_3_sentinel_f32();
    test_struct_4_sentinel_f32();
    test_struct_f32_align_8();
    test_struct_f32_align_8_f32_align_8();
    test_struct_f32_f32_f32_2();
    test_struct_f32_f32_f32_3();
    test_struct_f64();
    test_struct_f64_f64();
    test_struct_f64_f64_f64();
    test_struct_f64_f64_f64_f64();
    test_struct_f64_f64_f64_f64_f64();
    test_struct_1_f64();
    test_struct_2_f64();
    test_struct_3_f64();
    test_struct_4_f64();
    test_struct_5_f64();
    test_union_f64();
    test_struct_u32_union_u32_struct_u32_u32();
}
