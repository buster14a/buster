// Native hosted-CI history owner (#2823/#2824); no compiler/runtime policy.
// JSON/model headers own validation and physical identities; collect owns REST
// and startup joins; report owns bounded descriptive cohorts. This entry point
// separates read-only collection from a trusted data-branch publisher.
#include "ci_metrics_report.h"
#include "ci_metrics_test.h"
int main(int argc, char **argv)
{
    int result = 2;
    if (argc == 2 && cm_equal(argv[1], "--self-test")) result = cm_self_test();
    else fputs("usage: ci-metrics --self-test\n", stderr);
    return result;
}
