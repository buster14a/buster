// Aggregates too wide for a register, passed and returned by value.
//
// Every convention hands these over through memory, so the code for one call
// grows with the type: the callee reads its argument an eightbyte at a time
// through the incoming pointer, and a Win64 caller first copies each argument
// into a slot it owns. A handful of them in one function is more code than the
// flat per-instruction reserve the module code buffer used to be given, which
// is what this file is here to keep true.
//
// The alignment is deliberate. Sixty four is more than the stack pointer is
// worth at a call, so the caller-owned copy has to be reserved with room to
// spare and rounded up, and an over-aligned local has to be too.

typedef unsigned char u8;

typedef struct Wide Wide;
struct Wide
{
    _Alignas(64) u8 lanes[256];
};

static Wide wide_make(u8 base)
{
    Wide value;
    for (int lane = 0; lane < 256; lane += 1)
    {
        value.lanes[lane] = (u8)(base + lane);
    }
    return value;
}

// One argument, straight back out: the shortest path through the indirect
// argument and the indirect result at once.
static Wide wide_identity(Wide value)
{
    return value;
}

// Nine of them. Win64 has four argument registers and SystemV six, so whatever
// the convention, the later ones are handed over on the stack and the callee
// reads their pointers from there rather than from a register.
static Wide wide_ninth(Wide a, Wide b, Wide c, Wide d, Wide e, Wide f, Wide g, Wide h, Wide i)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    return i;
}

// Interleaved with integers, so both the register file and the stack have to
// advance past arguments of two different shapes in step.
static Wide wide_mixed(int first, Wide value, long long second, Wide other, int third)
{
    Wide sum;
    u8 scalar = (u8)(first + second + third);
    for (int lane = 0; lane < 256; lane += 1)
    {
        sum.lanes[lane] = (u8)(value.lanes[lane] + other.lanes[lane] + scalar);
    }
    return sum;
}

int main(void)
{
    Wide low = wide_make(0);
    Wide high = wide_make(100);

    Wide identity = wide_identity(low);
    for (int lane = 0; lane < 256; lane += 1)
    {
        if (identity.lanes[lane] != (u8)lane)
        {
            return 1;
        }
    }

    Wide ninth = wide_ninth(low, high, low, high, low, high, low, high, wide_make(7));
    for (int lane = 0; lane < 256; lane += 1)
    {
        if (ninth.lanes[lane] != (u8)(7 + lane))
        {
            return 2;
        }
    }

    Wide mixed = wide_mixed(1, low, 2, high, 3);
    for (int lane = 0; lane < 256; lane += 1)
    {
        if (mixed.lanes[lane] != (u8)((u8)lane + (u8)(100 + lane) + 6))
        {
            return 3;
        }
    }

    return 0;
}
