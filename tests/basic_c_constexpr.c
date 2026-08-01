struct Pair
{
    int first;
    int second;
};

constexpr int answer = 42;
constexpr int offset = answer + 5;
constexpr unsigned mask = 0xffu;
constexpr int negative = -3;
constexpr double exact_fraction = 3.25;
constexpr int values[] = {2, 3, 5, 7};
constexpr struct Pair pair = {answer, offset};
constexpr int* no_value = nullptr;

static_assert(answer == 42);
static_assert(offset == 47);
static_assert(negative == -3);
static_assert(sizeof(values) / sizeof(values[0]) == 4);

int sized_by_constexpr[answer];

static int local_constants(void)
{
    constexpr int local = answer - 2;
    int local_array[local];
    local_array[0] = local;
    return local_array[0];
}

int main(void)
{
    if (answer != 42 || offset != 47 || mask != 255u || negative != -3 || exact_fraction != 3.25)
    {
        return 1;
    }
    if (values[0] + values[1] + values[2] + values[3] != 17)
    {
        return 2;
    }
    if (pair.first != 42 || pair.second != 47)
    {
        return 3;
    }
    if (no_value != nullptr)
    {
        return 4;
    }
    if (local_constants() != 40)
    {
        return 5;
    }
    return sizeof(sized_by_constexpr) / sizeof(sized_by_constexpr[0]) == 42 ? 0 : 6;
}
