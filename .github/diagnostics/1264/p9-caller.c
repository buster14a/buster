typedef struct __attribute__((packed)) { char c; long long x; } P9;
int p9_probe(int, ...);
int main(void)
{
    P9 value = {3, 0x1122334455667788ll};
    return p9_probe(17, 1.5, value, 11ll, 2.5);
}
