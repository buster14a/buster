int first = 11;
int second = 22;
extern int external_value;
extern int external_function(void);

int local_function(void)
{
    return 5;
}

struct __attribute__((packed)) Entry
{
    int *pointer;
    unsigned char marker;
    int tag;
    unsigned char padding[3];
};
struct Entry entries[2] __attribute__((aligned(8))) = {{&first, 3, 7, {0}}, {&second, 4, 9, {0}}};
int *table[5] = {&first, 0, &second, &first, &external_value};
int (*callbacks[2])(void) = {local_function, external_function};

struct __attribute__((aligned(32))) Aligned
{
    int *pointer;
    unsigned char tail[3];
};
struct Aligned aligned = {&first, {1, 2, 3}};
