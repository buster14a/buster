int count;

struct Fields
{
    char head;
    int first[(1, count)], second[(2, count)], fixed[(3, 4)];
    short tail;
};

enum { Value = (4, 5) };
_Static_assert(sizeof(struct Fields) == 1 && _Alignof(struct Fields) == 1, "invalid record layout");

int *invalid_member(struct Fields *fields)
{
    return fields->first;
}

int valid_member(struct Fields *fields)
{
    return fields->fixed[0] + fields->tail;
}

int local_union(int count)
{
    int a[2];
    a[3] = 0;
    union Local
    {
        int array[(1, count)];
        int (*pointer)[(2, count)];
        int fixed[(3, 4)];
    };
    a[4] = 1;
    enum { LocalValue = (5, 6) };
    return sizeof(union Local);
}
