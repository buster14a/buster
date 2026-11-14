struct Bits
{
    unsigned bit : 1;
};

struct Bits object;
struct Bits make(void);

void bitfields(struct Bits *source)
{
    _Atomic __auto_type *direct = object.bit;
    _Atomic __auto_type *arrow = source->bit;
    _Atomic __auto_type *grouped = (object.bit);
    _Atomic __auto_type *nested = ((source->bit));
    _Atomic __auto_type *temporary = make().bit;
    _Atomic __auto_type *selected = __builtin_choose_expr(1, object.bit, 0);
    _Atomic __auto_type *generic = _Generic(1, int: object.bit);
    _Atomic __auto_type *extension = __extension__ object.bit;
    restrict _Atomic __auto_type *restricted = object.bit;
    _Atomic __auto_type *converted = (unsigned)object.bit;
    _Atomic __auto_type *comma = (1, object.bit);
    _Atomic __auto_type *conditional = source ? object.bit : source->bit;
    _Atomic __auto_type *assigned = (object.bit = 1);
    _Atomic __auto_type *real = __real__ object.bit;
    _Atomic __auto_type *statement = ({ object.bit; });
    _Atomic __auto_type *extension_comma = __extension__ (source++, object.bit);
    int later = independent_error;
}
