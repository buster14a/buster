// Pointer identity and scalar writes distinguish the original subobject from
// a vector snapshot. Rvalue lane reads retain their independent value image.
typedef unsigned int Wide __attribute__((vector_size(64)));
struct Storage
{
    unsigned before[16];
    Wide value;
    Wide array[2];
    unsigned after[16];
};
static struct Storage global;

static __attribute__((noinline)) Wide* member_address(struct Storage* storage)
{
    return &storage->value;
}

static __attribute__((noinline)) Wide* array_decay(struct Storage* storage)
{
    return storage->array;
}

static __attribute__((noinline)) Wide* array_address(struct Storage* storage, unsigned index)
{
    return &storage->array[index];
}

static __attribute__((noinline)) unsigned* scalar_decay(struct Storage* storage)
{
    return (unsigned*)storage->array;
}

static __attribute__((noinline)) Wide* dereference_address(Wide* value)
{
    return &*value;
}

static __attribute__((noinline)) unsigned rvalue_lane(Wide* value, unsigned lane)
{
    return (*value + 3)[lane];
}

static __attribute__((noinline)) unsigned captured_lane(Wide* value, unsigned lane)
{
    Wide captured = *value;
    ((unsigned*)value)[lane] = 999;
    return captured[lane];
}

static __attribute__((noinline)) int check(struct Storage* storage)
{
    int result = 0;
    unsigned* words = (unsigned*)storage;
    for (unsigned index = 0; index < 80; index += 1) { words[index] = 100 + index; }
    Wide* member = member_address(storage);
    Wide* array = array_decay(storage);
    Wide* second = array_address(storage, 1);
    unsigned* scalar = scalar_decay(storage);
    result |= (unsigned*)member != words + 16;
    result |= (unsigned*)array != words + 32;
    result |= (unsigned*)second != words + 48;
    result |= scalar != words + 32;
    result |= dereference_address(member) != member;
    for (unsigned lane = 0; lane < 16; lane += 1)
    {
        result |= rvalue_lane(member, lane) != 119 + lane;
        result |= captured_lane(member, lane) != 116 + lane;
        result |= words[16 + lane] != 999;
        ((unsigned*)member)[lane] = 200 + lane;
        ((unsigned*)array)[lane] = 300 + lane;
        ((unsigned*)second)[lane] = 400 + lane;
        scalar[16 + lane] += 7;
    }
    for (unsigned lane = 0; lane < 16; lane += 1)
    {
        result |= words[lane] != 100 + lane;
        result |= words[16 + lane] != 200 + lane;
        result |= words[32 + lane] != 300 + lane;
        result |= words[48 + lane] != 407 + lane;
        result |= words[64 + lane] != 164 + lane;
    }
    return result;
}

int main(void)
{
    struct Storage local;
    int result = check(&local) | check(&global);
    return result;
}
