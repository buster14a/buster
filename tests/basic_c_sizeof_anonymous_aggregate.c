// C11 permits an aggregate definition in a sizeof type name and in a
// compound literal. Both definitions below have eight-byte alignment and a
// sixteen-byte size on the supported targets, so keep the two forms as
// runtime assertions rather than allowing an unresolved expression to guess
// int.
int main(void)
{
    int type_name_form = (int)sizeof(struct { char a; long long b; });
    int literal_form = (int)sizeof((union { char a[9]; long long b; }){0});
    int result = 0;
    if (type_name_form != 16)
    {
        result = 1;
    }
    if (!result && literal_form != 16)
    {
        result = 2;
    }
    return result;
}
