/* Research only; included by the temporary, exact-base LLVM overlay.
 * Flags are supplied by the sibling research_config.h, never production defaults. */
#include "research_config.h"
#if R7_TRACE
#include <stdio.h>
#include <stdlib.h>
typedef struct R7Counts R7Counts;
struct R7Counts
{
    u64 passes, slots, attempts, resolved, dependency_checks;
    u64 intern_rows, intern_operands;
    u64 plan_nodes, plan_edges, delivered_edges, bucket_nodes, setup_bytes, fallback;
    u64 observed_edges;
    bool graph;
};
#define R7_ADD(c, f, n) ((c)->r7.f += (u64)(n))
#define R7_TYPE(c, i, p) do { if ((c)->r7.graph) { fprintf(stderr, "R7_TYPE %u %u\n", (unsigned)(i), (unsigned)(p)); } } while (0)
#else
#define R7_ADD(c, f, n) ((void)0)
#define R7_TYPE(c, i, p) ((void)0)
#endif
