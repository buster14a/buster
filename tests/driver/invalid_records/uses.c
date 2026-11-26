struct Record
{
    int broken[-1];
    int field : -2;
    void empty;
    int good;
};
struct Record object = { 1, 2, 3, 4, 5 };
struct Record initialized_record = { unbound_initializer, 2, 3 };
int *good_field_still_checked(struct Record *p)
{
    return p->good;
}
int *good_object_field_still_checked(void)
{
    return object.good;
}
int missing_field_still_checked(struct Record *p)
{
    return p->not_a_field;
}
int quiet_fields(struct Record *p)
{
    p->broken[0] = 1;
    p->field = 2;
    return p->empty + p->broken[0] + p->field + missing_in_body;
}
