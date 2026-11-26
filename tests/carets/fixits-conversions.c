// Conversion repairs belong to the primary diagnostic, including parentheses required by a unary edit (#2376).
void take_integer(int);
void take_pointer(int *);
void conversions(int *pointer, int value)
{
    take_integer(pointer);
    take_pointer(value);
    take_integer(&value);
    take_pointer(*pointer);
    take_integer(pointer + 1);
    take_integer((pointer + 1));
}
