int main(int argc, char** argv)
{
    (void)argv;
    _Static_assert(_Generic(__builtin_sqrt((double)argc), double: 1, default: 0), "unevaluated");
    return (int)__builtin_sqrt((double)argc);
}
