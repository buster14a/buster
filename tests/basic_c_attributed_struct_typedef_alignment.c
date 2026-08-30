// A typedef declared in the same declaration as a struct definition that
// carries an alignment-affecting attribute used to record the attribute's
// operand verbatim as the alias's own alignment. GNU `aligned` on a struct only
// raises, so the tag answered max(natural, operand) while the alias answered the
// literal operand -- one type with two alignments, and objects declared through
// the alias laid out to the smaller one (issue #819). The attribute belongs to
// the definition, which c_parse_layout_attributes already reads; the alias must
// not read it a second time.
//
// Both attribute positions a definition allows are covered -- between the
// keyword and the tag, and after the closing brace -- because they are one
// rule. The controls are the shapes that must keep working: a typedef whose
// `aligned` is on the declarator, which really does set the alias's alignment,
// and a typedef of a struct with no definition-attached attribute.

typedef struct __attribute__((aligned(4))) Raised
{
    long long bits : 13;
} Raised;

typedef struct MemberRaised
{
    _Alignas(32) signed char cell;
} __attribute__((packed, aligned(8))) MemberRaised;

typedef struct __attribute__((packed, aligned(2))) MemberDeclarator
{
    char a, b __attribute__((aligned(16))), c;
} MemberDeclarator;

typedef struct __attribute__((aligned(4))) Frame
{
    unsigned int cell __attribute__((aligned(16)));
} Frame;

typedef struct Global
{
    signed char cell __attribute__((aligned(32)));
} __attribute__((aligned(2))) Global;

// The declarator position is the alias's own, and there `aligned` replaces
// rather than raises: this is the record the definition-position scan must not
// be confused with.
typedef int CacheLine __attribute__((aligned(64)));
typedef struct Trailing
{
    int cell;
} Trailing __attribute__((aligned(64)));

static Global global_object;

int main(void)
{
    Frame frame_object;
    int result = 0;
    if (_Alignof(Raised) != _Alignof(struct Raised) || _Alignof(Raised) != 8)
    {
        result = 1;
    }
    else if (_Alignof(MemberRaised) != 32 || _Alignof(struct MemberRaised) != 32)
    {
        result = 2;
    }
    else if (_Alignof(MemberDeclarator) != 16 || _Alignof(struct MemberDeclarator) != 16)
    {
        result = 3;
    }
    else if (sizeof(Raised) != sizeof(struct Raised) || sizeof(MemberDeclarator) != sizeof(struct MemberDeclarator))
    {
        result = 4;
    }
    else if ((unsigned long long)(void*)&frame_object % 16u)
    {
        result = 5;
    }
    else if ((unsigned long long)(void*)&global_object % 32u)
    {
        result = 6;
    }
    else if (_Alignof(CacheLine) != 64 || _Alignof(Trailing) != 64)
    {
        result = 7;
    }
    return result;
}
