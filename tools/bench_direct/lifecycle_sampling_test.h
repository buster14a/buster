// Synthetic disabled sampling terminal recovery; no network or measurement.
// Included after core lifecycle fixtures, before lc_self_test. lcs_test joins
// owned research rows to platform source attempts and exercises terminal order.
#ifndef BUSTER_9700X_LIFECYCLE_SAMPLING_TEST_H
#define BUSTER_9700X_LIFECYCLE_SAMPLING_TEST_H
#define lcs_executor_0 lc_json_0
#define lcs_request_0 lc_json_30
#define lcs_executor_1 lc_json_0
BUSTER_GLOBAL_LOCAL const char lcs_request_1[] = "{\"id\":91,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-request.yml\",\"event\":\"pull_request\",\"head_branch\":\"codex/pull\",\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"completed\",\"conclusion\":\"success\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"actor\":{\"login\":\"other\",\"id\":99},\"triggering_actor\":{\"id\":39247043,\"login\":\"davidgmbb\"}}";
#define lcs_executor_2 lc_json_0
BUSTER_GLOBAL_LOCAL const char lcs_request_2[] = "{\"id\":91,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-request.yml\",\"event\":\"pull_request\",\"head_branch\":\"codex/pull\",\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"completed\",\"conclusion\":\"success\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"actor\":{\"id\":39247043,\"login\":\"davidgmbb\"},\"triggering_actor\":{\"login\":\"other\",\"id\":99}}";
#define lcs_executor_3 lc_json_0
#define lcs_request_3 lc_json_1
BUSTER_GLOBAL_LOCAL const char lcs_executor_4[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 91.2 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lcs_request_4[] = "{\"id\":91,\"run_attempt\":2,\"path\":\".github/workflows/9700x-direct-request.yml\",\"event\":\"pull_request\",\"head_branch\":\"codex/pull\",\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"completed\",\"conclusion\":\"success\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"actor\":{\"id\":39247043,\"login\":\"davidgmbb\"},\"triggering_actor\":{\"id\":39247043,\"login\":\"davidgmbb\"}}";
BUSTER_GLOBAL_LOCAL const char lcs_executor_5[] = "{\"id\":92,\"run_attempt\":2,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
#define lcs_request_5 lc_json_30
BUSTER_GLOBAL_LOCAL const char lcs_executor_6[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X direct workload benchmark\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
#define lcs_request_6 lc_json_30
BUSTER_GLOBAL_LOCAL const char lcs_executor_7[] = "{\"id\":92,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"9999999999999999999999999999999999999999\",\"status\":\"completed\",\"conclusion\":\"success\",\"display_title\":\"9700X request 91.1 head aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997}}";
BUSTER_GLOBAL_LOCAL const char lcs_request_7[] = "{\"id\":91,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-request.yml\",\"event\":\"pull_request\",\"head_branch\":\"codex/pull\",\"head_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"actor\":{\"id\":39247043,\"login\":\"davidgmbb\"},\"triggering_actor\":{\"id\":39247043,\"login\":\"davidgmbb\"}}";
#define lcs_executor_8 lcs_executor_7
#define lcs_request_8 lc_json_30
BUSTER_GLOBAL_LOCAL const char lcs_text_0[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:1:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_1[] = "queued";
BUSTER_GLOBAL_LOCAL const char lcs_text_2[] = "https://github.com/buster14a/buster/runs/10";
BUSTER_GLOBAL_LOCAL const char lcs_text_3[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_4[] = "in_progress";
BUSTER_GLOBAL_LOCAL const char lcs_text_5[] = "https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_6[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:acquire:0:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_7[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase acquire, packet 0; whole physical-job reservation 1800 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_8[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:2:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_9[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 2; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_10[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:confirm:39:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_11[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase confirm, packet 39; whole physical-job reservation 960 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_12[] = "completed";
BUSTER_GLOBAL_LOCAL const char lcs_text_13[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:1:93:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_14[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:1:91:93:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_15[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:1:91:92:2";
BUSTER_GLOBAL_LOCAL const char lcs_text_16[] = "buster-main-sampling-v1:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB:pilot:1:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_17[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:1:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_18[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:other:1:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_19[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:acquire:1:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_20[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:3:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_21[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:confirm:40:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_22[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:01:91:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_23[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:1:91:92:1:extra";
BUSTER_GLOBAL_LOCAL const char lcs_text_24[] = "buster-main-sampling-v1:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb:pilot:1:091:92:1";
BUSTER_GLOBAL_LOCAL const char lcs_text_25[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_26[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1\nWorkflow run 99 attempt 1: https://github.com/buster14a/buster/actions/runs/99/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_27[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_28[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1\nRequest run 93 attempt 1: https://github.com/buster14a/buster/actions/runs/93/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_29[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_30[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 2: https://github.com/buster14a/buster/actions/runs/91/attempts/2\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_31[] = "Unqualified sampling research; routine profile remains disabled.\n\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1";
BUSTER_GLOBAL_LOCAL const char lcs_text_32[] = "Unqualified sampling research; routine profile remains disabled.\n\nLifecycle protocol: sampling-terminal-native-v1.\nPhase pilot, packet 1; whole physical-job reservation 3000 seconds.\nNative Actions state shows scheduling and execution. This short controller has not measured a compiler.\n\nRequest run 91 attempt 1: https://github.com/buster14a/buster/actions/runs/91/attempts/1\nWorkflow run 92 attempt 1: https://github.com/buster14a/buster/actions/runs/92/attempts/1\nLifecycle protocol: terminal-native-v1.";
BUSTER_GLOBAL_LOCAL const char lcs_text_33[] = "https://github.com/buster14a/buster/runs/11";
BUSTER_GLOBAL_LOCAL const char lcs_text_34[] = "https://github.com/other/buster/runs/10";
BUSTER_GLOBAL_LOCAL const char lcs_text_35[] = "https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run";
BUSTER_GLOBAL_LOCAL const char lcs_text_36[] = "";
typedef struct LcsCase LcsCase;
struct LcsCase
{
    const char *label, *marker, *state, *url, *summary;
    unsigned platform, mode;
    int valid;
    unsigned closed, terminal, foreign, unavailable, requests;
};
BUSTER_GLOBAL_LOCAL const LcsCase lcs_cases[] =
{
    {"sampling queued cancellation without successor", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 1, 0, 0, 0, 6},
    {"sampling claimed cancellation keeps exact executor", lcs_text_0, lcs_text_4, lcs_text_5, lcs_text_3, 0, 0, 1, 1, 0, 0, 0, 6},
    {"sampling acquire boundary cancellation", lcs_text_6, lcs_text_1, lcs_text_2, lcs_text_7, 0, 0, 1, 1, 0, 0, 0, 6},
    {"sampling pilot boundary cancellation", lcs_text_8, lcs_text_1, lcs_text_2, lcs_text_9, 0, 0, 1, 1, 0, 0, 0, 6},
    {"sampling confirm boundary cancellation", lcs_text_10, lcs_text_1, lcs_text_2, lcs_text_11, 0, 0, 1, 1, 0, 0, 0, 6},
    {"sampling published success remains immutable", lcs_text_0, lcs_text_12, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 1, 0, 0, 4},
    {"sampling published failure remains immutable", lcs_text_0, lcs_text_12, lcs_text_2, lcs_text_3, 0, 7, 1, 0, 1, 0, 0, 4},
    {"sampling delayed duplicate callback remains terminal", lcs_text_0, lcs_text_12, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 1, 0, 0, 4},
    {"sampling fresh terminal after listing is immutable", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 1, 1, 0, 1, 0, 0, 5},
    {"sampling lost terminal write is observed once", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 2, 1, 1, 0, 0, 0, 7},
    {"sampling source cancelled overrides executor success", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 7, 0, 1, 1, 0, 0, 0, 6},
    {"sampling missing publisher after executor success is failure", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 8, 0, 1, 1, 0, 0, 0, 6},
    {"sampling duplicate owned rows both close", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 4, 1, 2, 0, 0, 0, 8},
    {"sampling fresh response retains exact check id", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 3, 0, 0, 0, 0, 0, 4},
    {"sampling API failures remain bounded and incomplete", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 5, 0, 0, 0, 0, 0, 6},
    {"sampling rejects other request", lcs_text_13, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects other executor", lcs_text_14, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects other executor attempt", lcs_text_15, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects uppercase campaign", lcs_text_16, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects short campaign", lcs_text_17, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects unsupported phase", lcs_text_18, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects acquire packet overflow", lcs_text_19, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects pilot packet overflow", lcs_text_20, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects confirm packet overflow", lcs_text_21, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects noncanonical packet decimal", lcs_text_22, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects marker suffix", lcs_text_23, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects noncanonical request decimal", lcs_text_24, lcs_text_1, lcs_text_2, lcs_text_3, 0, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects main push source", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 3, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects request rerun", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 4, 0, 1, 0, 0, 0, 1, 4},
    {"sampling rejects executor rerun", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 5, 0, 1, 0, 0, 0, 1, 4},
    {"sampling requires owner request actor", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 1, 0, 0, 0, 0, 0, 0, 2},
    {"sampling requires owner triggering actor", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 2, 0, 0, 0, 0, 0, 0, 2},
    {"sampling requires canonical trusted executor title", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 6, 0, 0, 0, 0, 0, 0, 1},
    {"sampling rejects missing executor join", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_25, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects conflicting executor join", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_26, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects duplicate executor join", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_27, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects conflicting request join", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_28, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects duplicate request join", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_29, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects wrong request attempt", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_30, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects missing sampling protocol", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_31, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects conflicting protocol", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_32, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects other check URL", lcs_text_0, lcs_text_1, lcs_text_33, lcs_text_3, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects other repository URL", lcs_text_0, lcs_text_1, lcs_text_34, lcs_text_3, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects generic workflow URL", lcs_text_0, lcs_text_1, lcs_text_35, lcs_text_3, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects empty details URL", lcs_text_0, lcs_text_1, lcs_text_36, lcs_text_3, 0, 0, 1, 0, 0, 1, 0, 5},
    {"sampling rejects other app", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 8, 1, 0, 0, 0, 1, 4},
    {"sampling rejects other source head", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 9, 1, 0, 0, 0, 1, 4},
    {"sampling rejects other check name", lcs_text_0, lcs_text_1, lcs_text_2, lcs_text_3, 0, 10, 1, 0, 0, 0, 1, 4},
};
BUSTER_GLOBAL_LOCAL char *lcs_row(const LcsCase *test, unsigned id, const char *state, const char *url)
{
    FILE *file = tmpfile();
    char *result = NULL;
    if (file)
    {
        fprintf(file, "{\"id\":%u,\"head_sha\":", id);
        cm_quote(file, test->mode == 9 ? "cccccccccccccccccccccccccccccccccccccccc" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        fputs(",\"name\":", file); cm_quote(file, test->mode == 10 ? "another research check" : "9700X compiler sampling research");
        fputs(",\"external_id\":", file); cm_quote(file, test->marker);
        fputs(",\"status\":", file); cm_quote(file, state);
        fprintf(file, ",\"app\":{\"id\":%u},\"details_url\":", test->mode == 8 ? 99u : 15368u);
        cm_quote(file, url);
        fputs(",\"output\":{\"title\":", file);
        cm_quote(file, cm_equal(state, "completed") ? (test->mode == 7 ? "Incomplete unqualified sampling packet" : "Valid unqualified sampling packet") : "Queued unqualified sampling research");
        fputs(",\"summary\":", file); cm_quote(file, test->summary); fputs("}", file);
        if (cm_equal(state, "completed"))
        {
            fputs(",\"conclusion\":", file); cm_quote(file, test->mode == 7 ? "failure" : "success");
        }
        fputc('}', file); result = cm_memory(file); fclose(file);
    }
    return result;
}
BUSTER_GLOBAL_LOCAL char *lcs_listing(const char *first, const char *second)
{
    FILE *file = tmpfile();
    char *result = NULL;
    if (file && first)
    {
        fprintf(file, "{\"total_count\":%u,\"check_runs\":[%s", second ? 2u : 1u, first);
        if (second) fprintf(file, ",%s", second);
        fputs("]}", file); result = cm_memory(file);
    }
    if (file) fclose(file);
    return result;
}
BUSTER_GLOBAL_LOCAL unsigned lcs_test(unsigned *count)
{
    const char *executors[] = {lcs_executor_0, lcs_executor_1, lcs_executor_2, lcs_executor_3, lcs_executor_4, lcs_executor_5, lcs_executor_6, lcs_executor_7, lcs_executor_8};
    const char *requests[] = {lcs_request_0, lcs_request_1, lcs_request_2, lcs_request_3, lcs_request_4, lcs_request_5, lcs_request_6, lcs_request_7, lcs_request_8};
    const unsigned executor_attempt[] = {1, 1, 1, 1, 1, 2, 1, 1, 1};
    const unsigned request_attempt[] = {1, 1, 1, 1, 2, 1, 1, 1, 1};
    const char jobs[] = "{\"total_count\":2,\"jobs\":[{\"id\":20,\"name\":\"Queue sampling qualification packet\",\"status\":\"completed\",\"conclusion\":\"success\",\"created_at\":\"2026-10-09T07:12:00Z\",\"started_at\":\"2026-10-09T07:12:04Z\",\"completed_at\":\"2026-10-09T07:12:10Z\"},{\"id\":21,\"name\":\"Sampling qualification packet\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"created_at\":\"2026-10-09T07:12:00Z\",\"started_at\":\"2026-10-09T08:24:08Z\",\"completed_at\":\"2026-10-09T08:37:48Z\"}]}";
    unsigned failures = 0;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(lcs_cases); ++i)
    {
        const LcsCase *test = &lcs_cases[i];
        char execution_path[128], request_path[128], jobs_path[128];
        snprintf(execution_path, sizeof(execution_path), "actions/runs/92/attempts/%u", executor_attempt[test->platform]);
        snprintf(request_path, sizeof(request_path), "actions/runs/91/attempts/%u", request_attempt[test->platform]);
        snprintf(jobs_path, sizeof(jobs_path), "actions/runs/92/attempts/%u/jobs?per_page=100", executor_attempt[test->platform]);
        char *row = lcs_row(test, 10, test->state, test->url);
        char *second = test->mode == 4 ? lcs_row(test, 11, "queued", "https://github.com/buster14a/buster/runs/11") : NULL;
        char *listed = lcs_listing(row, second);
        char *fresh = lcs_row(test, test->mode == 3 ? 12 : 10, test->mode == 1 ? "completed" : test->state, test->url);
        char *written = lcs_row(test, 10, "completed", test->url);
        char *other = test->mode == 4 ? lcs_row(test, 11, "completed", "https://github.com/buster14a/buster/runs/11") : NULL;
        CmResponse fixture[10] = {{0}};
        unsigned used = 0;
        fixture[used++] = (CmResponse){execution_path, "GET", executors[test->platform], 1, 0};
        if (test->requests > 1) fixture[used++] = (CmResponse){request_path, "GET", requests[test->platform], 1, 0};
        if (test->requests > 2)
        {
            fixture[used++] = (CmResponse){"commits/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", listed, 1, 0};
            if (!test->unavailable && !cm_equal(test->state, "completed"))
            {
                if (test->mode == 5)
                    for (unsigned retry = 0; retry < 3; ++retry)
                        fixture[used++] = (CmResponse){"check-runs/10", "GET", "{}", 0, 0};
                else fixture[used++] = (CmResponse){"check-runs/10", "GET", fresh, 1, 0};
                if (test->closed)
                {
                    fixture[used++] = (CmResponse){"check-runs/10", "PATCH", test->mode == 2 ? "{}" : written, test->mode != 2, 0};
                    if (test->mode == 2) fixture[used++] = (CmResponse){"check-runs/10", "GET", written, 1, 0};
                    if (test->mode == 4)
                    {
                        fixture[used++] = (CmResponse){"check-runs/11", "GET", second, 1, 0};
                        fixture[used++] = (CmResponse){"check-runs/11", "PATCH", other, 1, 0};
                    }
                }
            }
            if (test->valid) fixture[used++] = (CmResponse){jobs_path, "GET", jobs, 1, 0};
        }
        CmTransport transport = {0}; transport.deadline = cm_clock() + 180; transport.request_limit = 60;
        transport.fixture = fixture; transport.fixture_count = used;
        LcResult result = {0};
        int valid = listed && fresh && written && lc_recover(&transport, 92, executor_attempt[test->platform], &result);
        int pass = valid == test->valid && result.closed == test->closed && result.terminal == test->terminal &&
            result.foreign == test->foreign && result.unavailable == test->unavailable &&
            used == test->requests && transport.fixture_cursor == used && transport.requests == used;
        failures += !pass;
        printf("{\"fixture\":"); cm_quote(stdout, test->label);
        printf(",\"status\":\"%s\",\"valid\":%d,\"closed\":%u,\"terminal\":%u,\"foreign\":%u,\"unavailable\":%u,\"requests\":%u,\"expected\":%u}\n",
            pass ? "pass" : "fail", valid, result.closed, result.terminal, result.foreign, result.unavailable, transport.requests, test->requests);
        free(row); free(second); free(listed); free(fresh); free(written); free(other);
    }
    LcIdentity id = {0}; id.executor = 92; id.attempt = 1; id.request = 91; id.request_attempt = 1; id.pull = 1; id.sampling = 1;
    cm_copy(id.head, sizeof(id.head), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    cm_copy(id.trusted, sizeof(id.trusted), "9999999999999999999999999999999999999999"); lc_marker(&id);
    cm_copy(id.marker, sizeof(id.marker), lcs_text_0);
    cm_copy(id.outcome, sizeof(id.outcome), "cancelled");
    char *body = lc_body(&id, lcs_text_3, 0);
    failures += !body || !strstr(body, "\"conclusion\":\"cancelled\"") ||
        !strstr(body, "Incomplete unqualified sampling packet") || !strstr(body, "qualification=unqualified") ||
        !strstr(body, "routine_profile_enabled=false") || strstr(body, "Valid unqualified sampling packet") != NULL;
    free(body);
    cm_copy(id.outcome, sizeof(id.outcome), "success");
    body = lc_body(&id, lcs_text_3, 0);
    failures += !body || !strstr(body, "\"conclusion\":\"failure\"") || strstr(body, "\"conclusion\":\"success\"") != NULL; free(body);
    cm_copy(id.outcome, sizeof(id.outcome), "skipped");
    body = lc_body(&id, lcs_text_3, 0);
    failures += !body || !strstr(body, "\"conclusion\":\"failure\""); free(body);
    body = lc_body(&id, lcs_text_3, 1); failures += body != NULL; free(body);
    failures += !lc_physical_job("Sampling qualification packet") || lc_physical_job("Queue sampling qualification packet") ||
        lc_physical_job("Validate sampling packet evidence");
    CmResponse bound[] = {{"actions/runs/92/attempts/1", "GET", lcs_executor_0, 1, 0}};
    CmTransport limited = {0}; limited.fixture = bound; limited.fixture_count = 1; limited.request_limit = 1; limited.deadline = cm_clock() + 180;
    LcResult result = {0};
    failures += lc_recover(&limited, 92, 1, &result) || limited.requests != 1 || limited.fixture_cursor != 1;
    CmTransport expired = {0}; expired.fixture = bound; expired.fixture_count = 1; expired.request_limit = 60; expired.deadline = cm_clock() - 1;
    result = (LcResult){0};
    failures += lc_recover(&expired, 92, 1, &result) || expired.requests || expired.fixture_cursor;
    *count = (unsigned)BUSTER_ARRAY_LENGTH(lcs_cases) + 7;
    return failures;
}
#endif
