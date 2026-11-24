static int counter = 7;
static int other[4];
int main(int argc, char** argv)
{
    (void)argv;
    int* p = _Generic(argc, long: &counter, default: other);
    return p[0];
}
