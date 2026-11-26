typedef unsigned char Byte;
struct Valid
{
    int array[2][3];
    struct { int member; } nested;
    unsigned int all : 32;
    unsigned int : 0;
    Byte byte : 8;
    _Bool bit : 1;
    int (*function)(void);
    struct Forward *pointer;
    int tail[];
};
int valid(struct Valid *p)
{
    return p->array[1][2] + p->nested.member + p->byte + p->bit;
}
