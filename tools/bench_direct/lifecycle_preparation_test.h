// Synthetic exact preparation-research terminal recovery; no work/admission.
// Uses lifecycle_sampling_test.h's bounded platform/response fixture runner.
#ifndef BUSTER_9700X_LIFECYCLE_PREPARATION_TEST_H
#define BUSTER_9700X_LIFECYCLE_PREPARATION_TEST_H
BUSTER_GLOBAL_LOCAL const char lcp_text_0[] = "buster-compiler-preparation-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd:qualify:0:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_1[] = "queued";
BUSTER_GLOBAL_LOCAL const char lcp_text_2[] = "https://github.com/buster14a/buster/runs/10";
BUSTER_GLOBAL_LOCAL const char lcp_text_3[] = "Unqualified preparation research; routine profile remains disabled.\n\nLifecycle protocol: preparation-terminal-native-v1.\nPhase qualify, packet 0; whole physical-job reservation 5400 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcp_text_4[] = "in_progress";
BUSTER_GLOBAL_LOCAL const char lcp_text_5[] = "https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcp_text_6[] = "completed";
BUSTER_GLOBAL_LOCAL const char lcp_text_7[] = "buster-main-sampling-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd:pilot:0:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_8[] = "buster-compiler-preparation-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd:pilot:0:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_9[] = "buster-compiler-preparation-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd:qualify:1:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_10[] = "buster-compiler-preparation-v1:DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD:qualify:0:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_11[] = "buster-compiler-preparation-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd:qualify:0:091:92:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_12[] = "buster-compiler-preparation-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd:qualify:0:91:99:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_13[] = "buster-compiler-preparation-v2:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd:qualify:0:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcp_text_14[] = "Unqualified preparation research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase qualify, packet 0; whole physical-job reservation 5400 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcp_text_15[] = "Unqualified preparation research; routine profile remains disabled.\n\nLifecycle protocol: preparation-terminal-native-v1.\nPhase qualify, packet 0; whole physical-job reservation 5400 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcp_text_16[] = "Unqualified preparation research; routine profile remains disabled.\n\nLifecycle protocol: preparation-terminal-native-v1.\nPhase qualify, packet 0; whole physical-job reservation 5400 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1\nWorkflow run 99 attempt 1: https://github.com/buster14a/buster/actions/runs/99/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcp_text_17[] = "https://github.com/buster14a/buster/runs/11";
BUSTER_GLOBAL_LOCAL const LcsCase lcp_cases[] =
{
    {"preparation queued cancellation without successor", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 1, 0, 0, 0, 6},
    {"preparation claimed cancellation keeps exact executor", lcp_text_0, lcp_text_4, lcp_text_5, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 1, 0, 0, 0, 6},
    {"preparation published success remains immutable", lcp_text_0, lcp_text_6, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 1, 0, 0, 4},
    {"preparation published failure remains immutable", lcp_text_0, lcp_text_6, lcp_text_2, lcp_text_3, 0, 7, LC_RESEARCH_PREPARATION, 1, 0, 1, 0, 0, 4},
    {"preparation fresh terminal after listing is immutable", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 0, 1, LC_RESEARCH_PREPARATION, 1, 0, 1, 0, 0, 5},
    {"preparation source cancelled overrides executor success", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 7, 0, LC_RESEARCH_PREPARATION, 1, 1, 0, 0, 0, 6},
    {"preparation missing publisher after success is failure", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 8, 0, LC_RESEARCH_PREPARATION, 1, 1, 0, 0, 0, 6},
    {"preparation lost terminal write is observed once", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 0, 2, LC_RESEARCH_PREPARATION, 1, 1, 0, 0, 0, 7},
    {"preparation fresh response retains exact check id", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 0, 3, LC_RESEARCH_PREPARATION, 0, 0, 0, 0, 0, 4},
    {"preparation rejects sampling marker under preparation name", lcp_text_7, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects unsupported phase", lcp_text_8, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects packet overflow", lcp_text_9, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects uppercase plan SHA", lcp_text_10, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects noncanonical request decimal", lcp_text_11, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects other executor", lcp_text_12, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects other prefix version", lcp_text_13, lcp_text_1, lcp_text_2, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects other app", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 0, 8, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects other head", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 0, 9, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects other name", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 0, 10, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation rejects sampling protocol", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_14, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 1, 0, 5},
    {"preparation rejects duplicate request join", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_15, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 1, 0, 5},
    {"preparation rejects conflicting executor join", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_16, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 1, 0, 5},
    {"preparation rejects other canonical check URL", lcp_text_0, lcp_text_1, lcp_text_17, lcp_text_3, 0, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 1, 0, 5},
    {"preparation rejects source rerun", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 4, 0, LC_RESEARCH_PREPARATION, 1, 0, 0, 0, 1, 4},
    {"preparation requires owner request", lcp_text_0, lcp_text_1, lcp_text_2, lcp_text_3, 1, 0, LC_RESEARCH_PREPARATION, 0, 0, 0, 0, 0, 2},
    {"sampling rejects preparation marker", lcp_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, LC_RESEARCH_SAMPLING, 1, 0, 0, 0, 1, 4},
    {"sampling rejects preparation protocol", lcs_text_0, lcs_text_1, lcs_text_2, lcp_text_3, 0, 0, LC_RESEARCH_SAMPLING, 1, 0, 0, 1, 0, 5},
};
BUSTER_GLOBAL_LOCAL unsigned lcp_test(unsigned *count)
{
    unsigned failures = lcs_run_cases(lcp_cases, (unsigned)BUSTER_ARRAY_LENGTH(lcp_cases));
    LcIdentity id = {0}; id.executor = 92; id.attempt = 1; id.request = 91; id.request_attempt = 1;
    id.pull = 1; id.research = LC_RESEARCH_PREPARATION;
    cm_copy(id.head, sizeof(id.head), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    cm_copy(id.trusted, sizeof(id.trusted), "9999999999999999999999999999999999999999"); lc_marker(&id);
    cm_copy(id.marker, sizeof(id.marker), lcp_text_0); cm_copy(id.outcome, sizeof(id.outcome), "cancelled");
    char *body = lc_body(&id, lcp_text_3, 0);
    failures += !body || !strstr(body, "\"conclusion\":\"cancelled\"") ||
        !strstr(body, "Incomplete unqualified preparation research") || !strstr(body, "qualification=unqualified") ||
        !strstr(body, "routine_profile_enabled=false") || strstr(body, "Valid unqualified") != NULL;
    free(body);
    cm_copy(id.outcome, sizeof(id.outcome), "success");
    body = lc_body(&id, lcp_text_3, 0);
    failures += !body || !strstr(body, "\"conclusion\":\"failure\"") || strstr(body, "\"conclusion\":\"success\"") != NULL; free(body);
    cm_copy(id.outcome, sizeof(id.outcome), "skipped");
    body = lc_body(&id, lcp_text_3, 0);
    failures += !body || !strstr(body, "\"conclusion\":\"failure\""); free(body);
    body = lc_body(&id, lcp_text_3, 1); failures += body != NULL; free(body);
    *count = (unsigned)BUSTER_ARRAY_LENGTH(lcp_cases) + 4;
    return failures;
}
#endif
