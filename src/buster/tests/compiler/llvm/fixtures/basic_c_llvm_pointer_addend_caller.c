extern int values[3];
extern int *middle;
extern int read_middle(void);
struct Pair
{
    int left;
    int right;
};
extern struct Pair pair;
extern int *field;

int main(void)
{
    return read_middle() != 22 || middle != values + 1 || field != &pair.right || *field != 19;
}
