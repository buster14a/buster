int external_value = 17;
int external_function(void)
{
    return 8;
}

extern int first;
extern int second;
extern int local_function(void);
struct __attribute__((packed)) Entry
{
    unsigned char marker;
    int *pointer;
    int tag;
};
extern struct Entry entries[2];
extern int *table[5];
extern int (*callbacks[2])(void);
struct __attribute__((aligned(32))) Aligned
{
    int *pointer;
    unsigned char tail[3];
};
extern struct Aligned aligned;

int main(void)
{
    return sizeof(struct Entry) != 13 || sizeof(struct Aligned) != 32 ||
           (char*)&entries[0].pointer - (char*)&entries[0] != 1 ||
           (char*)&entries[1] - (char*)&entries[0] != 13 ||
           entries[0].marker != 3 || entries[1].marker != 4 ||
           *entries[0].pointer + entries[0].tag != 18 ||
           *entries[1].pointer + entries[1].tag != 31 ||
           entries[1].pointer != &second || table[0] != &first || table[1] != 0 ||
           table[2] != &second || table[3] != table[0] || table[4] != &external_value ||
           callbacks[0] != local_function || callbacks[0]() != 5 || callbacks[1]() != 8 ||
           aligned.pointer != &first || aligned.tail[0] != 1 || aligned.tail[1] != 2 || aligned.tail[2] != 3 ||
           ((unsigned long long)(void*)&aligned & 31) != 0;
}
