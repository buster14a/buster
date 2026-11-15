int direct[] = {};
typedef int Unknown[];
Unknown through_typedef = {};
int nested[][0] = {};
int fixed[0][0] = {};
int (*compound)[] = &(int[]){};
Unknown copied = (int[]){};
void local(void)
{
    int array[] = {};
    __auto_type pointer = &(int[]){};
    (void)array;
    (void)pointer;
}
