#define ND __attribute__((noderef))
typedef int ND *NP;
typedef NP Alias;
typedef NP (*Factory)(void);
struct Members { NP annotated; int *plain; NP nested[2]; };
NP global;
NP make(void);
int *make_plain(void);
int ND *direct_return(void);
void consume_plain(int *);
void consume_nd(NP);
NP return_plain(int *p) { return p; }
int *return_nd(NP p) { return p; }
void propagation(NP p, int *raw, NP *outer, Factory factory, struct Members *m, int which)
{
    Alias q = p;
    int *plain = p;
    NP assigned = raw;
    __auto_type inferred = p;
    __typeof__(p) typed = p;
    (void)*q;
    (void)*plain;
    (void)*assigned;
    (void)*inferred;
    (void)*typed;
    raw = p;
    p = raw;
    consume_plain(p);
    consume_nd(raw);
    (void)*(int *)p;
    (void)*(NP)raw;
    (void)*(which ? p : raw);
    (void)*(which ? raw : p);
    (void)*(which ? p : q);
    (void)*(p, q);
    (void)*(raw = p);
    (void)*(p = raw);
    (void)*(p + which);
    (void)*(which + p);
    (void)*p++;
    (void)*++p;
    (void)*make();
    (void)*direct_return();
    (void)*make_plain();
    (void)*factory();
    (void)**outer;
    (void)*m->annotated;
    (void)*m->plain;
    (void)*m->nested[which & 1];
}
