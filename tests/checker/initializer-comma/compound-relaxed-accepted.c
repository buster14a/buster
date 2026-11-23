int main(void)
{
    int value = 7;
    int *pointer = (int *){ 1 };
    long integer = (long){ &value };
    return (unsigned long)pointer != 1 || integer != (long)&value;
}
