#if __has_attribute(format)
#define PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))
#else
#define PRINTF_LIKE(f, a)
#endif
#if __has_attribute(__warn_unused_result__)
#define MUST_USE __attribute__((warn_unused_result))
#else
#define MUST_USE
#endif
#if __has_attribute(deprecated) && __has_extension(attribute_deprecated_with_message)
#define OLD(m) __attribute__((deprecated(m)))
#elif __has_attribute(deprecated)
#define OLD(m) __attribute__((deprecated))
#else
#define OLD(m)
#endif
#if __has_attribute(const) && __has_attribute(pure)
#define CONST __attribute__((const))
#define PURE __attribute__((pure))
#else
#define CONST
#define PURE
#endif
#if __has_attribute(noreturn)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif
#if __has_attribute(unused)
#define MAYBE __attribute__((unused))
#else
#define MAYBE
#endif
int log_line(const char*, ...) PRINTF_LIKE(1, 2);
MUST_USE int take(int);
OLD("use take") int give(int);
CONST int square(int);
PURE int peek(const int*);
NORETURN void die(void);
static int helper MAYBE;
int f(int n, const int* p)
{
    log_line("%d", 1.5);
    take(n);
    square(n);
    peek(p);
    if (n)
        return give(n);
    die();
}
