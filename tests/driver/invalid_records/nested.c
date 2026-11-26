struct Outer
{
    struct
    {
        int invalid[-2];
        int valid;
    };
    union
    {
        void bad;
        int good;
    } named;
    int last;
};
int invalid_nested(struct Outer *p)
{
    return p->invalid[0] + p->named.bad + missing_nested;
}
int *valid_nested(struct Outer *p)
{
    return p->valid;
}
void local_record(void)
{
    struct Local { int bad : -1; int good; } local;
    local.bad = 3;
    local.good = missing_local;
}
int tail = missing_tail;
