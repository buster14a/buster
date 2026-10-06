#if !WEAK_OMIT_REQUIRED
int mandatory_function(void)
{
    return 23;
}
#endif

#if WEAK_SUPPLY
int optional_data = 19;
int optional_function(void)
{
    return 17;
}
#endif

#if WEAK_OVERRIDE
int selected_data = 29;
int selected_function(void)
{
    return 29;
}
#endif

#if WEAK_OVERRIDE_ORDINARY
int ordinary_data = 31;
int ordinary_function(void)
{
    return 37;
}
#endif
