// Distinct operands own distinct ranges, including casts whose expressions extend beyond their type names (#2376).
struct Record { int member; };
typedef _Atomic(int) Atomic;
void unary(struct Record record, int value)
{
    (void)-record;
    (void)~(Atomic)value;
}
void binary(struct Record record, int value)
{
    (void)(record < value);
    (void)(value && (Atomic)value);
}
void conditional(struct Record record, int value)
{
    (void)(record ? 1 : 2);
    (void)(value ? record : 1);
}
void statements(struct Record record)
{
    if (record) { }
    switch (record) { default: break; }
}
void pointers(struct Record record, int *pointer, int value)
{
    (void)&(value + 1);
    (void)record[1];
    (void)pointer[record];
    (void)record[pointer];
}
int called_value(void) { return 123(); }
