/* Header-free C17; unsigned arithmetic has defined wraparound. */
typedef unsigned long long U64;
extern int printf(const char *, ...);
#define N 1024u
static U64 a[N], b[N];
static double d[N];
#define NOINLINE __attribute__((noinline))
static U64 parse(const char *s) { U64 x=0; while(*s>='0' && *s<='9') { x=x*10u+(U64)(*s-'0'); ++s; } return x; }
static void init(U64 seed) { unsigned i; for(i=0;i<N;++i) { seed=seed*6364136223846793005ull+1442695040888963407ull; a[i]=seed; b[i]=seed>>7u; d[i]=(double)(seed&1023u)*0.25; } }
NOINLINE U64 reduction(U64 reps,U64 seed) { U64 s=seed,r; unsigned i; for(r=0;r<reps;++r) for(i=0;i<N;++i) s+=a[i]; return s; }
NOINLINE U64 division(U64 reps,U64 seed) { U64 s=seed,r,divisor=(seed&31u)+1u; unsigned i; for(r=0;r<reps;++r) for(i=0;i<N;++i) s+=a[i]/divisor + b[i]/37u; return s; }
NOINLINE U64 licm(U64 reps,U64 seed) { U64 s=seed,r,x=seed+19u,y=seed*3u+7u; unsigned i; for(r=0;r<reps;++r) for(i=0;i<N;++i) s+=(a[i]+x*y)*(x+y)+(a[i]>>(seed&15u)); return s; }
NOINLINE U64 gvn(U64 reps,U64 seed) { U64 s=seed,r; unsigned i; for(r=0;r<reps;++r) for(i=0;i<N;++i) { U64 x=a[i], y=b[i], p=x*y+seed; s+=(p^y)+(x*y+seed)+(x*y+seed)*3u; } return s; }
static U64 helper(U64 x,U64 y) { return ((x^y)*33u + (x>>7u)) ^ (y<<3u); }
NOINLINE U64 inlining(U64 reps,U64 seed) { U64 s=seed,r; unsigned i; for(r=0;r<reps;++r) for(i=0;i<N;++i) s+=helper(a[i],seed)+helper(b[i],seed+1u); return s; }
NOINLINE U64 branches(U64 reps,U64 seed) { U64 s=seed,r; unsigned i; for(r=0;r<reps;++r) for(i=0;i<N;++i) { U64 x=a[i]^s; if(x&128u) s+=x*7u; else s^=x>>3u; } return s; }
NOINLINE U64 memory(U64 reps,U64 seed,U64 *src,U64 *dst) { U64 s=seed,r; unsigned i; for(r=0;r<reps;++r) for(i=0;i<N;++i) { unsigned j=(i*17u+(unsigned)seed)&(N-1u); dst[i]=src[j]+s; s+=dst[i]>>11u; } return s^dst[(unsigned)seed&(N-1u)]; }
NOINLINE U64 floating(U64 reps,U64 seed) { double s=(double)(seed&255u)*0.25; U64 r; unsigned i; union { double f; U64 u; } out; for(r=0;r<reps;++r) for(i=0;i<N;++i) s+=d[i]*0.5+0.125; out.f=s; return out.u; }
int main(int argc, char **argv)
{
    int status;
    if (argc != 4)
    {
        status = 2;
    }
    else
    {
        U64 k = parse(argv[1]), reps = parse(argv[2]), seed = parse(argv[3]);
        U64 result = 0;
        init(seed);
        status = 0;
        switch (k)
        {
            case 0: result = reduction(reps, seed); break;
            case 1: result = division(reps, seed); break;
            case 2: result = licm(reps, seed); break;
            case 3: result = gvn(reps, seed); break;
            case 4: result = inlining(reps, seed); break;
            case 5: result = branches(reps, seed); break;
            case 6: result = memory(reps, seed, a, b); break;
            case 7: result = floating(reps, seed); break;
            default: status = 3; break;
        }
        if (status == 0)
        {
            printf("%016llx\n", result);
        }
    }
    return status;
}
