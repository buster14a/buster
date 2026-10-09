// Synthetic lifecycle controls; production includes this only for --self-test.
// Fixtures never contact GitHub, execute artifacts, or certify performance.
#ifndef BUSTER_9700X_LIFECYCLE_TEST_H
#define BUSTER_9700X_LIFECYCLE_TEST_H
BUSTER_GLOBAL_LOCAL const char lc_json_0[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_1[] = "{\"id\":91,\"run_attempt\":1,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"completed\",\"conclusion\":\"success\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_2[] = "{\"total_count\":1,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_3[] = "{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}";
BUSTER_GLOBAL_LOCAL const char lc_json_4[] = "{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"},\"conclusion\":\"cancelled\"}";
BUSTER_GLOBAL_LOCAL const char lc_json_5[] = "{\"total_count\":2,\"jobs\":[{\"id\":5,\"name\":\"Show the main commit comparison check\",\"status\":\"completed\",\"conclusion\":\"success\",\"created_at\":\"2026-10-09T07:12:00Z\",\"started_at\":\"2026-10-09T07:12:04Z\",\"completed_at\":\"2026-10-09T07:12:10Z\"},{\"id\":6,\"name\":\"Compare the main commit compiler\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"created_at\":\"2026-10-09T07:12:00Z\",\"started_at\":\"2026-10-09T08:24:08Z\",\"completed_at\":\"2026-10-09T08:37:48Z\"}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_6[] = "{\"total_count\":1,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"in_progress\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_7[] = "{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"in_progress\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}";
BUSTER_GLOBAL_LOCAL const char lc_json_8[] = "{\"total_count\":1,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_9[] = "{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}";
BUSTER_GLOBAL_LOCAL const char lc_json_10[] = "{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"},\"conclusion\":\"cancelled\"}";
BUSTER_GLOBAL_LOCAL const char lc_json_11[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"success\",\"display_title\":\"9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_12[] = "{\"total_count\":1,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"},\"conclusion\":\"success\"}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_13[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"failure\",\"display_title\":\"9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_14[] = "{\"total_count\":1,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"},\"conclusion\":\"failure\"}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_15[] = "{\"total_count\":1,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"},\"conclusion\":\"cancelled\"}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_16[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 91.2 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_17[] = "{\"id\":91,\"run_attempt\":2,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"completed\",\"conclusion\":\"success\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_18[] = "{\"total_count\":1,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/99/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_19[] = "{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/99/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}";
BUSTER_GLOBAL_LOCAL const char lc_json_20[] = "{\"total_count\":2,\"check_runs\":[{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}},{\"id\":11,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}]}";
BUSTER_GLOBAL_LOCAL const char lc_json_21[] = "{\"id\":11,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"}}";
BUSTER_GLOBAL_LOCAL const char lc_json_22[] = "{\"id\":11,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"},\"conclusion\":\"cancelled\"}";
BUSTER_GLOBAL_LOCAL const char lc_json_23[] = "{\"id\":91,\"run_attempt\":1,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"status\":\"completed\",\"conclusion\":\"success\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_24[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":123},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_25[] = "{\"id\":91,\"run_attempt\":1,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lc_json_26[] = "{\"id\":10,\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:91.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/actions/runs/92/attempts/1\",\"output\":{\"summary\":\"Baseline recorded by the trusted authorizer.\"},\"conclusion\":\"failure\"}";
BUSTER_GLOBAL_LOCAL const char lc_json_27[] = "{\"total_count\":0,\"check_runs\":[]}";
BUSTER_GLOBAL_LOCAL const char lc_json_28[] = "{}";
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_0[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_2, 1, 0},
    {"check-runs/10", "GET", lc_json_3, 1, 0},
    {"check-runs/10", "PATCH", lc_json_4, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_1[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_6, 1, 0},
    {"check-runs/10", "GET", lc_json_7, 1, 0},
    {"check-runs/10", "PATCH", lc_json_4, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_2[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_8, 1, 0},
    {"check-runs/10", "GET", lc_json_9, 1, 0},
    {"check-runs/10", "PATCH", lc_json_10, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_3[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_11, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_12, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_4[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_13, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_14, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_5[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_15, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_6[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_12, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_7[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_16, 1, 0},
    {"actions/runs/91/attempts/2", "GET", lc_json_17, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_2, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_8[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_18, 1, 0},
    {"check-runs/10", "GET", lc_json_19, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_9[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_20, 1, 0},
    {"check-runs/10", "GET", lc_json_3, 1, 0},
    {"check-runs/10", "PATCH", lc_json_4, 1, 0},
    {"check-runs/11", "GET", lc_json_21, 1, 0},
    {"check-runs/11", "PATCH", lc_json_22, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_10[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_23, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_11[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_24, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_12[] =
{
    {"actions/runs/91/attempts/1", "GET", lc_json_25, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_8, 1, 0},
    {"check-runs/10", "GET", lc_json_9, 1, 0},
    {"check-runs/10", "PATCH", lc_json_10, 1, 0},
    {"actions/runs/91/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_13[] =
{
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"actions/runs/91/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_14[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_11, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_2, 1, 0},
    {"check-runs/10", "GET", lc_json_3, 1, 0},
    {"check-runs/10", "PATCH", lc_json_26, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_15[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_0, 1, 0},
    {"actions/runs/91/attempts/1", "GET", lc_json_1, 1, 0},
    {"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lc_json_27, 1, 0},
    {"actions/runs/92/attempts/1/jobs?per_page=100", "GET", lc_json_5, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lc_case_16[] =
{
    {"actions/runs/92/attempts/1", "GET", lc_json_28, 0, 0},
    {"actions/runs/92/attempts/1", "GET", lc_json_28, 0, 0},
    {"actions/runs/92/attempts/1", "GET", lc_json_28, 0, 0},
};
typedef struct LcTestCase LcTestCase;
struct LcTestCase { const char *label; const CmResponse *fixture; unsigned count; uint64_t run, attempt; int valid; unsigned closed, terminal, foreign, unavailable; };
BUSTER_GLOBAL_LOCAL int lc_self_test(void)
{
    const LcTestCase cases[] =
    {
        {"cancel before host assignment", lc_case_0, 6, 92, 1, 1, 1, 0, 0, 0},
        {"cancel during execution", lc_case_1, 6, 92, 1, 1, 1, 0, 0, 0},
        {"final cancelled benchmark with no successor", lc_case_2, 6, 92, 1, 1, 1, 0, 0, 0},
        {"published success remains terminal", lc_case_3, 4, 92, 1, 1, 0, 1, 0, 0},
        {"published failure remains terminal", lc_case_4, 4, 92, 1, 1, 0, 1, 0, 0},
        {"duplicate delivery remains terminal", lc_case_5, 4, 92, 1, 1, 0, 1, 0, 0},
        {"terminal before setup remains terminal", lc_case_6, 4, 92, 1, 1, 0, 1, 0, 0},
        {"stale request attempt cannot overwrite newer one", lc_case_7, 4, 92, 1, 1, 0, 0, 0, 1},
        {"other executor binding is preserved", lc_case_8, 5, 92, 1, 1, 0, 0, 1, 0},
        {"duplicate owned checks both close", lc_case_9, 8, 92, 1, 1, 2, 0, 0, 0},
        {"unknown head rejects publication", lc_case_10, 2, 92, 1, 0, 0, 0, 0, 0},
        {"wrong repository rejects publication", lc_case_11, 1, 92, 1, 0, 0, 0, 0, 0},
        {"main request cancelled without executor", lc_case_12, 5, 91, 1, 1, 1, 0, 0, 0},
        {"successful request does not claim measurement", lc_case_13, 2, 91, 1, 1, 0, 0, 0, 0},
        {"successful executor without publisher closes failure", lc_case_14, 6, 92, 1, 1, 1, 0, 0, 0},
        {"missing check is an explicit gap", lc_case_15, 4, 92, 1, 1, 0, 0, 0, 1},
        {"API error remains incomplete", lc_case_16, 3, 92, 1, 0, 0, 0, 0, 0},
    };
    unsigned failures = 0;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(cases); ++i)
    {
        CmTransport transport = {0}; transport.deadline = cm_clock() + 180; transport.request_limit = 60;
        transport.fixture = cases[i].fixture; transport.fixture_count = cases[i].count;
        LcResult result = {0};
        int valid = lc_recover(&transport, cases[i].run, cases[i].attempt, &result);
        int pass = valid == cases[i].valid && result.closed == cases[i].closed && result.terminal == cases[i].terminal &&
            result.foreign == cases[i].foreign && result.unavailable == cases[i].unavailable &&
            transport.fixture_cursor == cases[i].count && transport.requests <= 10;
        failures += !pass;
        printf("{\"fixture\":"); cm_quote(stdout, cases[i].label);
        printf(",\"status\":\"%s\",\"valid\":%d,\"closed\":%u,\"terminal\":%u,\"foreign\":%u,\"unavailable\":%u,\"requests\":%u,\"consumed\":%u,\"expected\":%u}\n",
            pass ? "pass" : "fail", valid, result.closed, result.terminal, result.foreign, result.unavailable,
            transport.requests, transport.fixture_cursor, cases[i].count);
    }
    LcIdentity id = {0};
    int title_valid = lc_title("9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &id);
    failures += !title_valid || id.request != 91 || id.request_attempt != 1;
    failures += lc_title("9700X request 091.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &id);
    failures += lc_title("9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa extra", &id);
    cm_copy(id.head, sizeof(id.head), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"); id.request = 91; id.request_attempt = 1; id.executor = 92; id.attempt = 1;
    cm_copy(id.outcome, sizeof(id.outcome), "cancelled"); lc_marker(&id);
    char *body = lc_body(&id, "Baseline preserved.", 0);
    failures += !body || !strstr(body, "\"conclusion\":\"cancelled\"") || !strstr(body, "Baseline preserved.") || strstr(body, "\"conclusion\":\"success\"") != NULL;
    free(body);
    cm_copy(id.outcome, sizeof(id.outcome), "success");
    body = lc_body(&id, "", 0); failures += !body || !strstr(body, "\"conclusion\":\"failure\""); free(body);
    FILE *missing = tmpfile();
    if (missing)
    {
        lc_seconds(missing, "", "2026-10-09T08:24:08Z"); char *text = cm_memory(missing);
        failures += !text || !cm_equal(text, "null"); free(text); fclose(missing);
    }
    else ++failures;
    printf("{\"schema\":\"buster-9700x-lifecycle-fixtures-v1\",\"cases\":%zu,\"failures\":%u,\"performance_validation\":\"unavailable\"}\n", BUSTER_ARRAY_LENGTH(cases) + 6, failures);
    int result = failures ? 2 : 0;
    return result;
}
#endif
