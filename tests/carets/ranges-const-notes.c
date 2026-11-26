// Const declarations own their note ranges; the rejected assignment and lvalue cast own independent ranges (#2376).
const int constant = 1;
struct Record
{
    const int field;
};
void assignments(struct Record *record, int value)
{
    constant = value;
    record->field = value;
    (int)value = 2;
}
