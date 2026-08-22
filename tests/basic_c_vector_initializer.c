typedef float Float4 __attribute__((vector_size(16)));

typedef int Int4 __attribute__((vector_size(16)));

typedef int Int8 __attribute__((vector_size(32)));

typedef int Int16 __attribute__((vector_size(64)));

typedef struct VectorPack
{
    Int4 vector;
    int tail;
} VectorPack;

typedef union VectorBox
{
    Int4 vector;
    int lanes[4];
} VectorBox;

static Int4 global_full = {1, 2, 3, 4};
static Int4 global_partial = {7, 8};
static Int4 global_zero = {0};
static Float4 global_float = {1.5f, 2.5f, 3.5f, 4.5f};
static VectorPack global_pack = {{9, 10, 11, 12}, 13};
static VectorPack global_pack_designated = {.tail = 18, .vector = {14, 15, 16, 17}};
static VectorPack global_pack_flat = {41, 42, 43, 44, 45};
static VectorPack global_pack_compound = {.vector = (Int4){61, 62, 63, 64}, .tail = 65};
static Int4 global_array[2] = {{21, 22, 23, 24}, {25, 26, 27, 28}};
static Int4 global_inferred[] = {{31, 32, 33, 34}, {35, 36, 37, 38}, {39, 40, 41, 42}};
static Int8 global_wide = {1, 2, 3, 4, 5, 6, 7, 8};
static Int16 global_very_wide = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

Int4 vector_pass_through(Int4 value)
{
    return value;
}

int main(void)
{
    Int4 local_full = {1, 2, 3, 4};
    Int4 local_partial = {7, 8};
    Int4 local_zero = {0};
    Float4 local_float = {1.5f, 2.5f, 3.5f, 4.5f};
    VectorPack local_pack = {{9, 10, 11, 12}, 13};
    VectorPack local_pack_designated = {.tail = 18, .vector = {14, 15, 16, 17}};
    Int4 local_array[2] = {{21, 22, 23, 24}, {25, 26, 27, 28}};
    Int4 local_compound = (Int4){51, 52, 53, 54};
    Int4 local_argument = vector_pass_through((Int4){71, 72, 73, 74});
    VectorBox local_box = {{81, 82, 83, 84}};
    Int8 local_wide = {1, 2, 3, 4, 5, 6, 7, 8};
    Int16 local_very_wide = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    for (int lane = 0; lane < 4; lane += 1)
    {
        if (local_full[lane] != lane + 1)
            return 1;
        if (global_full[lane] != lane + 1)
            return 2;
        if (local_partial[lane] != (lane < 2 ? lane + 7 : 0))
            return 3;
        if (global_partial[lane] != (lane < 2 ? lane + 7 : 0))
            return 4;
        if (local_zero[lane] != 0)
            return 5;
        if (global_zero[lane] != 0)
            return 6;
        if (local_float[lane] != (float)lane + 1.5f)
            return 7;
        if (global_float[lane] != (float)lane + 1.5f)
            return 8;
        if (local_pack.vector[lane] != lane + 9)
            return 9;
        if (global_pack.vector[lane] != lane + 9)
            return 10;
        if (local_pack_designated.vector[lane] != lane + 14)
            return 11;
        if (global_pack_designated.vector[lane] != lane + 14)
            return 12;
        if (global_pack_flat.vector[lane] != lane + 41)
            return 14;
        if (global_pack_compound.vector[lane] != lane + 61)
            return 15;
        if (local_array[0][lane] != lane + 21 || local_array[1][lane] != lane + 25)
            return 16;
        if (global_array[0][lane] != lane + 21 || global_array[1][lane] != lane + 25)
            return 17;
        if (global_inferred[2][lane] != lane + 39)
            return 18;
        if (local_compound[lane] != lane + 51)
            return 19;
        if (local_argument[lane] != lane + 71)
            return 20;
        if (local_box.lanes[lane] != lane + 81)
            return 21;
    }
    for (int lane = 0; lane < 8; lane += 1)
    {
        if (local_wide[lane] != lane + 1 || global_wide[lane] != lane + 1)
            return 22;
    }
    for (int lane = 0; lane < 16; lane += 1)
    {
        if (local_very_wide[lane] != lane + 1 || global_very_wide[lane] != lane + 1)
            return 23;
    }
    if (local_pack.tail != 13 || global_pack.tail != 13)
        return 24;
    if (local_pack_designated.tail != 18 || global_pack_designated.tail != 18)
        return 25;
    if (global_pack_flat.tail != 45)
        return 26;
    if (global_pack_compound.tail != 65)
        return 27;
    if (sizeof(global_inferred) != 3 * sizeof(Int4))
        return 28;
    return 0;
}
