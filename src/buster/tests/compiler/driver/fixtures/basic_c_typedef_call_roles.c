typedef int (*Callback)(int, int, int);
typedef int Function(int, int, int);

static int calls;

static int sum(int first, int second, int third)
{
    calls += 1;
    return first + second + third;
}

struct Matrix
{
    Callback first[2][2];
    Callback second[2][2];
};

static int select_callback(struct Matrix *matrix, int select)
{
    Callback (*table)[2] = select ? matrix->first : matrix->second;
    typedef Callback Local;
    Local (*local)[2] = table;
    Callback (slot) = local[1][0];
    Function *function = sum;
    int value = slot(1, 2, 3) + ((Callback)table[0][1])(4, 5, 6);
    value += function(7, 8, 9);
    {
        Callback Callback = sum;
        value += Callback(10, 11, 12);
    }
    return value;
}

int main(void)
{
    struct Matrix matrix = {
        {{sum, sum}, {sum, sum}},
        {{sum, sum}, {sum, sum}}
    };
    int first = select_callback(&matrix, 0);
    int second = select_callback(&matrix, 1);
    return first != 78 || second != 78 || calls != 8;
}
