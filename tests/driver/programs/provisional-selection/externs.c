extern int unused_extern(void);
static const char kept_text[] = "kept";
static int helper(int x) { return x + 1; }
int main(int argc, char** argv)
{
    (void)argv;
    int r = _Generic(argc, long: unused_extern, default: helper)(argc);
    const char* s = _Generic(argc, long: "only in an unselected association", default: kept_text);
    return r + s[0];
}
