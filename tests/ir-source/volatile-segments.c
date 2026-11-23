// #3329: a volatile object in a segment's address space (__seg_gs), whose copies go through check_lower_copy_segmented.
// A compound assignment computed in a complex type reads the object once into a frame object and writes it back once,
// and a volatile struct's assignment and read are copies: each piece's LOAD and SET is a volatile access, as clang -O0's
// `load volatile`/`store volatile` in addrspace(256) are. A unit of its own: a segment's space in a unit makes all of its
// copies take the segmented path. correctness.tests counts the printed volatile accesses through %gs; the program is
// only compiled, since its pointers would address %gs.
struct Pair
{
    long a;
    long b;
};

volatile double __seg_gs* segment_double;
volatile struct Pair __seg_gs* segment_pair;
struct Pair sink;

void complex_update(_Complex double z)
{
    *segment_double += z;
}

void assigned(struct Pair value)
{
    *segment_pair = value;
}

void read(void)
{
    sink = *segment_pair;
}
