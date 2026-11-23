struct Pair { int *first; int *second; };
int value;
struct Pair global = (struct Pair){ &value, 0 };

int main(void)
{
    int local = 7;
    struct Pair pairs[2] = { (struct Pair){ &local, 0 }, { 0, &local } };
    struct Pair nested = (struct Pair){ ({ (void)0; &local; }), 0 };
    int scalar = (int){ local };
    return scalar != 7 || *pairs[0].first != 7 || *pairs[1].second != 7 ||
           *nested.first != 7 || global.first != &value || global.second != 0;
}
