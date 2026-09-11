struct Value { unsigned char bytes[3]; };

int read_value(volatile struct Value value)
{
    value.bytes[0] = 7;
    return value.bytes[0];
}

int main(void)
{
    struct Value value = {{1, 2, 3}};
    return read_value(value) != 7;
}
