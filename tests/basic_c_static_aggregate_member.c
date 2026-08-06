typedef struct LargeAggregate
{
    char padding[1024 * 1024];
    int first;
    int second;
} LargeAggregate;

static LargeAggregate global_aggregate = {
    .first = 19,
    .second = 23,
};

static int read_fields(void)
{
    return global_aggregate.first + global_aggregate.second + global_aggregate.first;
}

int main(void)
{
    return read_fields() == 61 ? 0 : 1;
}
