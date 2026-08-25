typedef long z_off64_t;

extern void abort(void) __attribute__((__noreturn__));

struct gzFile_s
{
    unsigned have;
    unsigned char* next;
    z_off64_t pos;
};
typedef struct gzFile_s* gzFile;

static int fallback(int value)
{
    return value + 1;
}

static int indirect(int (*function)(int), int value)
{
    return (*function)(value);
}

static int gzgetc(gzFile file)
{
    (void)file;
    return ' ';
}

#define ZLIB_GZGETC(g) ((g)->have ? ((g)->have--, (g)->pos++, *((g)->next)++) : (gzgetc)(g))
#define ZLIB_ASSERT(expression) ((void)sizeof((expression) ? 1 : 0), __extension__ ({ if (expression) ; else abort(); }))

static void assertion_continuation(int* value)
{
    ZLIB_ASSERT(value != (void*)0);
    *value = 7;
}

int main(void)
{
    int count[4] = {0};
    int lens[4] = {0, 2, 1, 3};
    count[lens[1]]++;

    unsigned char bytes[8] = {0};
    unsigned char* scan = bytes;
    unsigned char* match = bytes;
    scan += 2, match++;

    struct gzFile_s state = {0};
    gzFile file = &state;
    unsigned char character = ' ';
    file->have = 1;
    file->next = &character;
    int gz_value = ZLIB_GZGETC(file);
    file->have = 1;
    file->next = &character;
    int gz_direct = ZLIB_GZGETC(file) == ' ';
    ZLIB_ASSERT(gz_direct == 1);
    int continued = 0;
    assertion_continuation(&continued);
    return indirect(fallback, 2) != 3 || gz_value != ' ' || gz_direct != 1 ||
           continued != 7 || scan != bytes + 2 || match != bytes + 1 || count[lens[1]] != 1;
}
