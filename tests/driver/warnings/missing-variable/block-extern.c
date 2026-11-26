void before_definitions(void)
{
    extern int before_initialized;
    extern int before_tentative;
    {
        extern int nested;
    }
}
int before_initialized = 1;
int before_tentative;
int nested;
int before_late_block;
int repeated_around_block;
int already_initialized = 2;
void between_definitions(void)
{
    extern int before_late_block;
    extern int repeated_around_block;
    extern int already_initialized;
    extern int different_object;
}
int repeated_around_block;
int different_object;
static int internal;
void internal_declaration(void)
{
    extern int internal;
    internal = 3;
}
void automatic_shadow(void)
{
    int shadowed = 4;
    {
        extern int shadowed;
    }
    (void)shadowed;
}
int shadowed;
