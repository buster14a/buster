/* Linux AArch64 (AAPCS64): alignment 4, size 8. */
struct sample { char a; unsigned int : 0; char b; };
int main(void)
{
    return sizeof(struct sample) != 8;
}
