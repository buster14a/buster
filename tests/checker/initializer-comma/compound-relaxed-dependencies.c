void compound_relaxed_dependencies(int value)
{
    float scalar = (int *){ 1 };
    int *integer = (long){ &value };
    float pair[2] = { (int *){ 2 }, (int *){ 3 } };
}
