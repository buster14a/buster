extern int declared;
int declared = 1;
int first = 2, second = 3;
int tentative;
int repeated;
int repeated;
static int internal;
static int internal_initializer = 4;
static int inherited_internal;
extern int inherited_internal;
int read_definitions(void);
int read_definitions(void)
{
    static int local;
    int automatic = 5;
    return declared + first + second + tentative + repeated + internal + internal_initializer + inherited_internal + local + automatic;
}
