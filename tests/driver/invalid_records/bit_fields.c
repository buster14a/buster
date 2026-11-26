typedef unsigned char Byte;
struct Named
{
    int negative : -1;
    unsigned int wide : 200;
    int zero : 0;
    float real : 2;
    _Bool boolean : 2;
    Byte byte : 9;
    int sentinel : 0xffffffffu;
    int tail;
};
struct Unnamed
{
    int : -2;
    int : 99;
    float : 1;
    int tail;
};
int after = missing_after_fields;
int read_invalid(struct Named *p)
{
    return p->negative + p->wide + p->zero + p->real + missing_after_uses;
}
