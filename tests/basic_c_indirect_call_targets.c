// Calls whose callee is a postfix chain rather than a name.  SQLite reaches
// its virtual-table and VFS methods this way: `pColl[j].xDel(pColl[j].pUser)`
// calls through a member of an array element, and the memdb VFS forwards to
// the VFS underneath it with `ORIGVFS(pVfs)->xDlOpen(ORIGVFS(pVfs), zPath)`,
// which expands to a member of a parenthesized cast.  The barrier and the
// assert shape below come from the same file: `sqlite3MemoryBarrier` is
// `__sync_synchronize()`, and a debug build's asserts are GNU statement
// expressions in the body of an unbraced loop.
struct Collation
{
    void (*destroy)(void *user);
    void *user;
};

struct Vfs
{
    void *(*open)(struct Vfs *vfs, const char *path);
    void *app_data;
};

#define ORIGVFS(p) ((struct Vfs *)((p)->app_data))

static int destroyed;
static int values[3] = {1, 2, 4};
static struct Collation collations[3];

static void destroy(void *user) { destroyed += *(int *)user; }

static void *open_underlying(struct Vfs *vfs, const char *path)
{
    (void)vfs;
    return (void *)path;
}

static struct Vfs base = {open_underlying, 0};
static struct Vfs wrapper = {0, &base};

static void *open_through(struct Vfs *vfs, const char *path) { return ORIGVFS(vfs)->open(ORIGVFS(vfs), path); }

static int checked(const int *entries, int count)
{
    int index;
    int total = 0;
    // A GNU statement expression in the body of an unbraced loop that is
    // itself the substatement of an `if`: the brace group belongs to the
    // expression, not to the loop, so the statement ends at its semicolon.
    if (entries) for (index = 0; index < count; index += 1) total += ({ int entry = entries[index]; entry * 2; });
    return total;
}

int main(void)
{
    int index;
    void *opened;
    for (index = 0; index < 3; index += 1)
    {
        collations[index].destroy = destroy;
        collations[index].user = &values[index];
    }
    for (index = 0; index < 3; index += 1)
    {
        if (collations[index].destroy)
        {
            collations[index].destroy(collations[index].user);
        }
    }
    if (destroyed != 7) return 1;

    opened = open_through(&wrapper, "path");
    if (opened != (void *)"path" && *(const char *)opened != 'p') return 2;

    __sync_synchronize();

    if (checked(values, 3) != 14) return 3;
    if (checked(0, 3) != 0) return 4;
    return 0;
}
