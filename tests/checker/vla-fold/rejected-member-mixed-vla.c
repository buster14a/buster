int count;

struct Fields
{
    int outer[(1, count)][(2, 2)];
    int inner[(3, 2)][(4, count)];
    int (*pointer)[(5, count)];
    int fixed[(6, 2)];
};

int local_struct(int count)
{
    typedef int Variable[(1, count)];
    struct Fields
    {
        Variable inherited;
        int outer[(1, count)][(2, 2)];
        int inner[(3, 2)][(4, count)];
    };
    struct Fields fields;
    return fields.inherited[0] + fields.outer[0][0];
}
