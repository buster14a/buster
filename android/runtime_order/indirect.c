// GNU IFUNC definition for android/runtime_order, compiled by the NDK's clang
// and linked into the Buster-linked probe. The resolver hands back a different
// function on any later call, so a second resolution or a call that reaches
// the resolver itself changes the probe's result.
static volatile int resolutions;

static int twice(int x) { return x * 2; }
static int thrice(int x) { return x * 3; }

static int (*resolve_scale(void))(int)
{
    resolutions += 1;
    return resolutions == 1 ? thrice : twice;
}

int scale(int) __attribute__((ifunc("resolve_scale")));

int runtime_order_resolutions(void)
{
    return resolutions;
}
