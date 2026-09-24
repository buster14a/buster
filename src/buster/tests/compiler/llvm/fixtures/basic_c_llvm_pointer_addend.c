int values[3] = {11, 22, 33};
int *middle = values + 1;

struct Pair
{
    int left;
    int right;
};
struct Pair pair = {7, 19};
int *field = &pair.right;

int read_middle(void)
{
    return *middle;
}
