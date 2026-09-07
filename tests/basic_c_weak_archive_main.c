extern int optional_hook(void) __attribute__((weak));
#ifdef REQUIRE_ARCHIVE_MEMBER
int required_hook(void);
#endif

int main(void)
{
    int failed = 0;
#ifdef REQUIRE_ARCHIVE_MEMBER
    failed = required_hook() != 42;
#endif
#ifdef EXPECT_ARCHIVE_HOOK
    failed = failed || optional_hook == 0;
#else
    failed = failed || optional_hook != 0;
#endif
    return failed;
}
