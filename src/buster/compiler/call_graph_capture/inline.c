// A fixture for ./build.sh check-call-graph, never built into the compiler: the IR capture must keep what the
// production build may inline. again's body exists only for inlining (GNU extern inline), and strong.c defines the
// out-of-line again, so the recursion recurse -> again -> recurse is there only in this unit's inlined body, which a
// capture at -O0 doesn't emit at all.
int recurse(int n);

extern inline __attribute__((gnu_inline)) int again(int n)
{
    return recurse(n);
}

int recurse(int n)
{
    int result = 1;
    if (n > 1)
    {
        result = again(n - 1) + again(n - 2);
    }
    return result;
}
