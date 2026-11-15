int global[0];
int computed[2 - 2];
typedef int Zero[0];
Zero reused;
struct Record { int member; int tail[0]; };
union Union { int member[0]; };
int (*pointer)[0];
void parameters(int first[0], int second[static 0], int third[const 0]);
int size = sizeof(int[0]);
typedef __typeof__(int[0]) TypeofZero;
int matrix[0][0];
int nested_size[sizeof(int[0])];
void local(int length)
{
    int array[0];
    int variable[length];
    (void)array;
    (void)variable;
}
