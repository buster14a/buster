#pragma weak call = function
#pragma weak data = array

int function(int value) { return value + 1; }
long array[2] = { 3, 4 };
int (*saved_call)(int) = call;
long (*saved_array)[2] = &data;
_Static_assert(sizeof(data) == sizeof(array), "clone has the target's array type");

long use(int n)
{
    {
        long data = n;
        n = call(data);
    }
    return saved_call(n) + data[0] + (*saved_array)[1];
}

extern long data[2];
int call(int);
long again(void) { return call(1) + data[1]; }
