// Independent declaration reductions; baseline compiler failures are expected.
#if CASE == 0
struct match { struct match *next; char name[]; };
int main(void)
{
    struct match head = { .next = (void *)0 }, *tail = &head;
    return tail->next != (void *)0;
}
#else
struct dso { void *base; long values[3]; int marker; char buf[]; };
int main(void)
{
    struct dso *p, temp_dso = {0};
    p = &temp_dso;
    return p->base != (void *)0 || p->values[0] != 0 ||
           p->values[1] != 0 || p->values[2] != 0 || p->marker != 0;
}
#endif
