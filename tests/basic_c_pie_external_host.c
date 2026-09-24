int buster_pie_call_import(int value);

int buster_pie_import(int value)
{
    return value + 2;
}

int main(void)
{
    return buster_pie_call_import(40) != 43;
}
