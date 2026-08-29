// C11 7.19p3 lets offsetof's member designator subscript as well as select,
// and the constant walk that folds a *static* initializer only followed the
// `.member` half.  In an ordinary expression the subscript already worked, so
// the two paths disagreed and only the initializer form was rejected.
//
// musl's ioctl.c is where that matters:
//
//     OFFS(offsetof(struct v4l2_event, ts[0]), offsetof(struct v4l2_event, ts[1]))
//
// sits inside a file-scope `static const` table.  <stddef.h> spells offsetof
// as pointer arithmetic unless __GNUC__ is defined and as __builtin_offsetof
// when it is, so this shape only reaches the folder in the GNU spelling --
// which is every dialect now (see tests/basic_c_type_generic_math.c).

// Only types whose size and alignment are the same on every target this
// builds for, so the expected offsets below can be written down.
struct event
{
    unsigned a;
    double b[8];
    unsigned c[2], ts[2], d[9];
};

struct row
{
    struct event events[3];
};

// The initializer under test.  `static const` so it is folded rather than
// computed, which is the whole point.
static const unsigned long offsets[] = {
    __builtin_offsetof(struct event, ts[0]),
    __builtin_offsetof(struct event, ts[1]),
    __builtin_offsetof(struct event, c),
    __builtin_offsetof(struct event, b[2]),
    __builtin_offsetof(struct row, events[1].ts[1]),
};

int main(void)
{
    if (offsets[0] != __builtin_offsetof(struct event, ts[0]) || offsets[0] != 8 + 8 * 8 + 2 * 4)
    {
        return 1;
    }
    // The subscript has to scale by the element, not merely be accepted.
    if (offsets[1] != offsets[0] + sizeof(unsigned))
    {
        return 2;
    }
    if (offsets[2] != 8 + 8 * 8)
    {
        return 3;
    }
    if (offsets[3] != 8 + 2 * 8)
    {
        return 4;
    }
    // A subscript that is not the last step of the designator, so the walk has
    // to carry the element type into the member that follows it.
    if (offsets[4] != sizeof(struct event) + __builtin_offsetof(struct event, ts[1]))
    {
        return 5;
    }
    return 0;
}
