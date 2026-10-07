extern int weak_presence(void);
extern int weak_direct(void);
extern int weak_indirect(void);
extern int weak_selected(void);
extern int weak_ordinary(void);
extern int weak_required(void);

int main(void)
{
    int failures = 0;
    failures += weak_presence() != (WEAK_SUPPLY ? 3 : 0);
    failures += weak_direct() != (WEAK_SUPPLY ? 36 : 0);
    failures += weak_indirect() != (WEAK_SUPPLY ? 36 : 0);
    failures += weak_selected() != (WEAK_OVERRIDE ? 58 : 22);
    failures += weak_ordinary() != 28;
    failures += weak_required() != 23;
    return failures;
}
