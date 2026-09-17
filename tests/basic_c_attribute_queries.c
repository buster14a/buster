// #666: the query selects the attribute, so a false negative must lose an
// observable semantic property rather than merely changing a token count.
// The driver checks serialized bindings on ELF/Mach-O/COFF and executes the
// native image in all four allocator modes. Missing either destructor leaves
// a nonzero exit status; the final destructor reports success through _exit.

#if __has_attribute(noreturn)
#define QUERY_NORETURN __attribute__((noreturn))
#else
#define QUERY_NORETURN
#endif

#if __has_attribute(__noreturn__)
#define QUERY_NORETURN_RESERVED __attribute__((__noreturn__))
#else
#define QUERY_NORETURN_RESERVED
#endif

#if __has_attribute(weak)
#define QUERY_WEAK __attribute__((weak))
#else
#define QUERY_WEAK
#endif

#if __has_attribute(__weak__)
#define QUERY_WEAK_RESERVED __attribute__((__weak__))
#else
#define QUERY_WEAK_RESERVED
#endif

#if __has_attribute(alias)
#define QUERY_ALIAS(value) __attribute__((alias(value)))
#else
#define QUERY_ALIAS(value)
#endif

#if __has_attribute(__alias__)
#define QUERY_ALIAS_RESERVED(value) __attribute__((__alias__(value)))
#else
#define QUERY_ALIAS_RESERVED(value)
#endif

#if __has_attribute(constructor)
#define QUERY_CONSTRUCTOR(value) __attribute__((constructor(value)))
#else
#define QUERY_CONSTRUCTOR(value)
#endif

#if __has_attribute(__constructor__)
#define QUERY_CONSTRUCTOR_RESERVED(value) __attribute__((__constructor__(value)))
#else
#define QUERY_CONSTRUCTOR_RESERVED(value)
#endif

#if __has_attribute(destructor)
#define QUERY_DESTRUCTOR(value) __attribute__((destructor(value)))
#else
#define QUERY_DESTRUCTOR(value)
#endif

#if __has_attribute(__destructor__)
#define QUERY_DESTRUCTOR_RESERVED(value) __attribute__((__destructor__(value)))
#else
#define QUERY_DESTRUCTOR_RESERVED(value)
#endif

QUERY_NORETURN extern void _exit(int status);
QUERY_NORETURN_RESERVED void query_abort(int status) { _exit(status); }

int query_target(int value) { return value + 7; }
int query_object = 17;
extern int query_alias(int value) QUERY_ALIAS("query_target");
extern int query_object_alias QUERY_ALIAS_RESERVED("query_object");
// Negative COFF control: accepted weak syntax still serializes as strong.
__attribute__((weak)) int query_unconditional_weak = 23;
QUERY_WEAK int query_weak_object = 5;
QUERY_WEAK_RESERVED int query_weak_function(void) { return 11; }
// Attribute names outside their lists remain ordinary, strong definitions.
int weak = 13;
int alias = 19;

static int constructed;
static int destroyed;
static int main_seen;
QUERY_CONSTRUCTOR(101) static void query_construct(void) { constructed += 1; }
QUERY_CONSTRUCTOR_RESERVED(150) static void query_construct_reserved(void) { constructed += 2; }
QUERY_DESTRUCTOR_RESERVED(200) static void query_destroy_reserved(void) { destroyed += 1; }
QUERY_DESTRUCTOR(101) static void query_report(void)
{
    query_abort(constructed == 3 && main_seen == 1 && destroyed == 1 ? 0 : 10);
}

int main(void)
{
    int status = 1;
    if (constructed == 3 && destroyed == 0 && query_alias(1) == 8 &&
        query_alias == query_target && query_object_alias == 17 &&
        &query_object_alias == &query_object && query_weak_object == 5 &&
        query_weak_function() == 11 && weak == 13 && alias == 19)
    {
        main_seen = 1;
        status = 9;
    }
    return status;
}
