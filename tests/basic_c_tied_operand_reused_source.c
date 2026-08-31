// CPython's insert_to_usedpool indexes `usedpools[size + size]`: both
// operands of the two-address add are the same promoted local.  The tied
// transfer saw the local's last textual use at that row, released its
// register as dying, and discarded the only dirty copy -- the second
// operand then reloaded from a home slot nothing ever stored, and the
// allocator walked into unmapped memory.  A vreg another use slot of the
// same row still reads has not died for the transfer.

struct pool
{
    void* a;
    void* b;
    struct pool* nextpool;
    struct pool* prevpool;
    unsigned arenaindex;
    unsigned szidx;
};

static void* table[16];

__attribute__((noinline)) static void* probe(struct pool* pool)
{
    unsigned size = pool->szidx;
    return table[size + size];
}

int main(void)
{
    struct pool pool = {0};
    pool.szidx = 3;
    table[6] = (void*)&table[6];
    if (probe(&pool) != (void*)&table[6])
    {
        return 1;
    }
    return 0;
}
