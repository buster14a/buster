// An absolute address with no symbol is a constant in a static initializer:
// a nonzero integer cast to a pointer, the address of a member of a null
// object (the classic offsetof), and either of those cast back to an integer.
// `long long` keeps every value the same width on LP64 and LLP64 targets.

struct Record
{
    int first;
    char name[6];
    long long last;
};

char* integer_address = (char*)8;
long long integer_address_value = (long long)(void*)16;
int* member_address = &((struct Record*)0)->first;
char* name_address = (char*)&((struct Record*)0)->name[3];
long long last_offset = (long long)&((struct Record*)0)->last;
unsigned long long name_offset = (unsigned long long)&((struct Record*)0)->name[3];
long long scaled_offset = (long long)(&((struct Record*)0)->last) * 2;
long long negative_address = (long long)(char*)-1;

// Pointer arithmetic on an absolute address scales by the element size.
int* scaled_pointer = (int*)16 + 1;
char* back_pointer = (char*)100 - 4;
long long pointer_difference = (char*)100 - (char*)40;

struct Holder
{
    char* pointer;
    long long offset;
    int* member;
};

struct Holder holders[2] = {
    {(char*)24, (long long)(char*)40, &((struct Record*)0)->first},
    {&((char*)0)[7], (long long)&((struct Record*)0)->last, 0},
};

int main(void)
{
    if ((long long)integer_address != 8 || integer_address_value != 16)
    {
        return 1;
    }
    if ((long long)member_address != 0 || (long long)name_address != 7)
    {
        return 2;
    }
    if (last_offset != 16 || name_offset != 7 || scaled_offset != 32 || negative_address != -1)
    {
        return 3;
    }
    if ((long long)holders[0].pointer != 24 || holders[0].offset != 40 || holders[0].member != 0)
    {
        return 4;
    }
    if ((long long)holders[1].pointer != 7 || holders[1].offset != 16 || holders[1].member != 0)
    {
        return 5;
    }
    if ((long long)scaled_pointer != 20 || (long long)back_pointer != 96 || pointer_difference != 60)
    {
        return 6;
    }
    return 0;
}
