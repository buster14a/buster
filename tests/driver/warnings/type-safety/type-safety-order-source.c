typedef int Datatype;
void put(void *buffer, Datatype type) __attribute__((pointer_with_type_tag(tag, 1, 2)));
void early(long *l) { put(l, 5); }
static const Datatype tag_int __attribute__((type_tag_for_datatype(tag, int))) = 5;
void middle(long *l) { put(l, 5); }
static const Datatype tag_long __attribute__((type_tag_for_datatype(tag, long))) = 5;
void late(long *l, int *i) {
    put(l, 5);
    put(i, 5);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtype-safety"
    put(i, 5);
#pragma clang diagnostic pop
    put(i, 0x5);
    put(i, 5u);
    put(i, (5));
    put(i, '\5');
    put(i, +5);
    put(i, (Datatype)5);
    {
        static const Datatype local __attribute__((type_tag_for_datatype(tag, int))) = 6;
    }
    put(l, 6);
}
struct ompi { int a; };
extern struct ompi tag_short __attribute__((type_tag_for_datatype(tag, short)));
extern struct ompi tag_short;
void put2(void *buffer, struct ompi *type) __attribute__((pointer_with_type_tag(tag, 1, 2)));
void put2(void *buffer, struct ompi *type);
void put2(void *buffer, struct ompi *type) __attribute__((argument_with_type_tag(tag, 1, 2)));
void again(int *i) { put2(i, &tag_short); }
