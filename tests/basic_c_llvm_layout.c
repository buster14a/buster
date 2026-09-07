/* Exact storage size, explicit gaps, field construction, and aggregate copies. */
struct __attribute__((packed)) Packed { char tag; int value; };
struct __attribute__((aligned(32))) Overaligned { int value; };
struct Natural { char tag; int value; };
struct __attribute__((packed)) Nested { char tag; struct Natural value; };
struct Packed packed[2] = {{1, 11}, {2, 22}};
struct Overaligned aligned[2] = {{33}, {44}};
long packed_stride(struct Packed *p, int i) { return (char *)(p + i) - (char *)p; }
long aligned_stride(struct Overaligned *p, int i) { return (char *)(p + i) - (char *)p; }
int packed_read(struct Packed *p, int i) { return p[i].value; }
int main(void)
{
    struct Natural natural = {5, 66};
    struct Nested nested = {7, {8, 99}};
    struct Natural copy = natural;
    struct Packed packed_copy = packed[1];
    struct Overaligned aligned_copy = aligned[1];
    int failed = packed_stride(packed, 1) != 5 || aligned_stride(aligned, 1) != 32;
    failed |= packed_read(packed, 1) != 22 || packed_copy.tag != 2 || packed_copy.value != 22;
    failed |= aligned_copy.value != 44 || copy.tag != 5 || copy.value != 66;
    failed |= nested.tag != 7 || nested.value.tag != 8 || nested.value.value != 99;
    packed[0] = packed_copy;
    failed |= packed[0].tag != 2 || packed[0].value != 22;
    return failed;
}
