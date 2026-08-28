// A struct or union tag first named inside a type name declares an incomplete
// type there (C11 6.7.2.3p8). musl's `src/ldso/dlinfo.c` writes
//
//   *(struct link_map **)res = dso;
//
// without ever including the header that defines `struct link_map`; only
// pointers to it are ever formed, so the incomplete type is all the
// translation unit needs. The type-name resolver looked the tag up in the
// types the parser had recorded and gave up when it found none, failing as
// "could not lower unbound identifier 'struct'".
//
// The values are checked at run time: a cast that silently resolved to the
// wrong element type would still compile, and only a round trip through the
// pointer shows the addresses that were actually stored and loaded.

struct Declared
{
    int first;
    int second;
};

static struct Declared subject = {11, 22};

// The two shapes side by side: a tag that is declared and one that is only
// ever named in a cast.
static void store_declared(void *slot, void *object)
{
    *(struct Declared **)slot = object;
}

static void store_opaque(void *slot, void *object)
{
    *(struct Opaque **)slot = object;
}

static void *load_opaque(void *slot)
{
    return *(struct Opaque **)slot;
}

// The same tag named again in a different function has to be the same type,
// or the two pointers below would not be assignable to one another.
static void copy_opaque(void *destination, void *source)
{
    struct Opaque **target = (struct Opaque **)destination;
    *target = *(struct Opaque **)source;
}

// A union tag takes the same path.
static void store_opaque_union(void *slot, void *object)
{
    *(union OpaqueUnion **)slot = object;
}

int main(void)
{
    void *slot = 0;
    struct Declared *declared = 0;
    store_declared(&slot, &subject);
    declared = slot;
    if (declared != &subject || declared->first != 11 || declared->second != 22)
    {
        return 1;
    }

    slot = 0;
    store_opaque(&slot, &subject);
    if (slot != (void *)&subject)
    {
        return 2;
    }
    if (load_opaque(&slot) != (void *)&subject)
    {
        return 3;
    }

    void *second_slot = 0;
    copy_opaque(&second_slot, &slot);
    if (second_slot != (void *)&subject)
    {
        return 4;
    }

    slot = 0;
    store_opaque_union(&slot, &subject);
    if (slot != (void *)&subject)
    {
        return 5;
    }

    // The opaque pointer is a pointer: it compares, converts to void *, and
    // survives a null.
    struct Opaque *opaque = (struct Opaque *)&subject;
    if ((void *)opaque != (void *)&subject || (struct Opaque *)0 != 0)
    {
        return 6;
    }
    if (sizeof(struct Opaque *) != sizeof(void *))
    {
        return 7;
    }

    return 0;
}
