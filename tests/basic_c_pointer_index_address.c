// `&p[n]` is address arithmetic: C never reads p[n] to compute it.  SQLite's
// unixRemapfile takes `&pOrig[nReuse]` where nReuse is the whole length of a
// memory mapping, so a load emitted for the index faults on the first byte
// past the mapping rather than merely wasting a read.  The bound of a local
// array may also be spelled with offsetof, as SQLite sizes its parser's save
// buffer with `sizeof(Parse) - offsetof(Parse, sLastToken)`.
typedef unsigned char u8;
typedef long long i64;

struct Region
{
    void *base;
    i64 size;
};

struct Parse
{
    int header;
    double weight;
    char name[24];
    int tail_first;
    int tail_second;
};

#define PARSE_TAIL_SIZE (sizeof(struct Parse) - __builtin_offsetof(struct Parse, tail_first))

static u8 storage[64];
static struct Region region;

static u8 *region_end(struct Region *from)
{
    u8 *base = (u8 *)from->base;
    u8 *end = 0;
    if (base)
    {
        i64 reuse = from->size;
        u8 *request = &base[reuse];
        end = request;
    }
    return end;
}

static unsigned long save_size(void)
{
    char buffer[PARSE_TAIL_SIZE];
    unsigned long index;
    for (index = 0; index < sizeof(buffer); index += 1)
    {
        buffer[index] = (char)index;
    }
    return sizeof(buffer) + (unsigned long)buffer[0];
}

int main(void)
{
    static char file_scope_buffer[PARSE_TAIL_SIZE];
    u8 *cursor;
    region.base = storage;
    region.size = (i64)sizeof(storage);

    if (region_end(&region) != storage + sizeof(storage)) return 1;
    region.base = 0;
    if (region_end(&region) != 0) return 2;

    cursor = &storage[16];
    if (cursor - storage != 16) return 3;
    if (&storage[sizeof(storage)] != storage + sizeof(storage)) return 4;
    if (&cursor[-16] != storage) return 5;

    if (save_size() != PARSE_TAIL_SIZE) return 6;
    if (sizeof(file_scope_buffer) != PARSE_TAIL_SIZE) return 7;
    if (PARSE_TAIL_SIZE != 2 * sizeof(int)) return 8;
    return 0;
}
