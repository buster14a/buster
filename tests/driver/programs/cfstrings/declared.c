extern const int __CFConstantStringClassReference[];
typedef struct { const int* isa; int flags; const char* str; long length; } CF;
const int* class_reference(void);
int tested(int n) { const CF* c = (const CF*)__builtin___CFStringMakeConstantString("d"); return n + (c->isa == class_reference()) * 10 +
(c->isa == __CFConstantStringClassReference) * 100 + (int)c->length * 1000; }
