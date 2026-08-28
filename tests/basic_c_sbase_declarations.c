// The declaration shapes sbase's sources put in front of the C frontend, each
// of which used to drop the declaration it belongs to. They are gathered here
// because their failures were all the same shape: the declarator scan lost
// the declared name, so the entity was never created and every later use
// reported an undeclared identifier somewhere else entirely.
//
// Each check returns a distinct exit status, so a failing run names the shape
// rather than the file.

// 1. An object is in scope inside its own initializer. <sys/queue.h> spells a
//    list head this way -- TAILQ_HEAD_INITIALIZER names the list it
//    initializes -- and sbase's od, sort and cron all declare one at file
//    scope. The declarator scan keeps the last occurrence of the name so a
//    `struct head { ... } head;` tag cannot outrank the declarator, which used
//    to make the occurrence in the initializer win.
struct list
{
    int* first;
    int** last;
};
static struct list head = {0, &head.first};

// 2. The same shape with a tag that repeats the object's name, which is what
//    the scan's last-occurrence rule exists for.
struct entry
{
    struct entry* next;
};
static struct entry entry = {&entry};

// 3. A prototype's `char **` and a definition's `char *argv[]` are one
//    function type: a parameter declared as an array is adjusted to the
//    corresponding pointer (C 6.7.6.3p7). sbase declares enmasse and
//    cryptcheck one way in a header and defines them the other way.
static int adjusted(int, char**, int (*)(const char*));
static int reference(const char* text)
{
    return text[0];
}
static int adjusted(int count, char* items[], int (*action)(const char*))
{
    return count + action(items[0]);
}

// 4. GNU attributes may sit between the aggregate keyword and its tag. glibc
//    declares `struct __attribute__((__may_alias__)) sockaddr_storage` that
//    way, which is how sbase's tftp acquires it; read as an object
//    declaration instead of a type definition, the attribute's own name
//    became the declared object and the aggregate was never defined.
struct __attribute__((__may_alias__)) aliased
{
    int first;
    int second;
};
static struct aliased aliased_object = {3, 4};

struct header
{
    char name[8];
    char checksum[6];
};

int main(void)
{
    if (head.last != &head.first || head.first != 0)
    {
        return 1;
    }
    if (entry.next != &entry)
    {
        return 2;
    }
    // The initializer is one string literal inside braces, which sizes a
    // character array but fills one element of an array of pointers; only the
    // element type separates the two, and the inference used to claim both.
    char* items[] = {"argument"};
    if (adjusted(2, items, reference) != 2 + 'a' || sizeof items != sizeof(char*))
    {
        return 3;
    }
    char text[] = {"abc"};
    if (sizeof text != 4 || text[2] != 'c')
    {
        return 9;
    }
    if (sizeof(struct aliased) != 2 * sizeof(int) || aliased_object.second != 4)
    {
        return 4;
    }

    // 5. A block-scope array of function pointers. sbase's test dispatches on
    //    one (`int (*narg[])(char *[])`), and the declarator's name sits
    //    before its array suffix rather than at the end of the parenthesized
    //    group.
    int (*table[])(const char*) = {reference, reference};
    if (table[1]("b") != 'b')
    {
        return 5;
    }

    // 6. An enumeration defined inline in a declaration carries `=` signs of
    //    its own, inside braces. sbase's dd declares its conversion flags this
    //    way; the initializer scan used to stop at the first enumerator's sign
    //    and treat the rest of the definition as an initializer.
    enum
    {
        LOWER = 1 << 0,
        UPPER = 1 << 1,
        SWAP = 1 << 2,
    } conversion = 0;
    conversion = UPPER | SWAP;
    if (conversion != 6)
    {
        return 6;
    }

    // 7. An unparenthesized sizeof over a member expression as an array bound,
    //    followed by a second declarator. sbase's tar sizes a scratch buffer
    //    from a header field this way. An unfolded bound made the array
    //    variable-length, which a declarator list rejects.
    struct header record;
    char buffer[sizeof record.checksum], *cursor;
    cursor = buffer;
    cursor[0] = 'x';
    if (sizeof buffer != 6 || buffer[0] != 'x')
    {
        return 7;
    }

    struct header* pointer = &record;
    char indirect[sizeof pointer->name], *tail;
    tail = indirect;
    tail[0] = 'y';
    if (sizeof indirect != 8 || indirect[0] != 'y')
    {
        return 8;
    }

    return 0;
}
