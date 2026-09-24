extern int buster_pie_import(int value);

__attribute__((noinline)) static int buster_pie_local_increment(int value)
{
    return value + 1;
}

int buster_pie_call_import(int value)
{
    return buster_pie_import(buster_pie_local_increment(value));
}
