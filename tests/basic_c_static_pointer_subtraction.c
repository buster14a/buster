// CPython's obmalloc seeds its pool list heads statically: every entry is
// `(poolp)((uint8_t*)&(pools.used[2*i]) - 2*sizeof(block*))`, an address
// minus a constant.  The fold's overflow bound computed INT64_MIN minus
// the accumulated addend -- itself an overflow for any positive addend --
// so every negative count was rejected and the element fell through to a
// symbol-only relocation with its addend gone: each head pointed at the
// object's base and the first insertion walked into it.  The invariant
// checked here is the trick's own: head->nextpool must alias the slot
// pair the head was derived from.
#include <stdio.h>

typedef unsigned char uint8_t;
typedef struct pool* poolp;
struct pool
{
    void* a;
    void* b;
    poolp nextpool;
    poolp prevpool;
};

struct pools_state
{
    poolp used[8];
};

struct outer
{
    int pad;
    struct pools_state pools;
};

#define PTA(pools, x) ((poolp)((uint8_t*)&(pools.used[2 * (x)]) - 2 * sizeof(void*)))
#define PT(p, x) PTA(p, x), PTA(p, x)

static struct outer runtime = {
    .pools = {.used = {PT(runtime.pools, 0), PT(runtime.pools, 1), PT(runtime.pools, 2), PT(runtime.pools, 3)}},
};

static unsigned words[8];
static void* forward = (void*)(&words[2] + 2);
static void* backward = (void*)(&words[2] - 2);
static void* negative_count = (void*)(&words[4] + -3);

int main(void)
{
    for (int index = 0; index < 8; index += 1)
    {
        poolp head = runtime.pools.used[index];
        if ((void*)&head->nextpool != (void*)&runtime.pools.used[2 * (index / 2)])
        {
            return 1;
        }
    }
    if (forward != (void*)&words[4] || backward != (void*)&words[0] || negative_count != (void*)&words[1])
    {
        return 2;
    }
    printf("static pointer subtraction ok\n");
    return 0;
}
