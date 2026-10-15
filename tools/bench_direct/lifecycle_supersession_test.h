// Incident-derived pending-main supersession controls (#3885).
// Run/check IDs, native annotation markup and announce summary come from
// 38297977458.1 / check 114973570744; REST records retain only consumed fields.
// All fixtures are synthetic transport responses; no physical work or API write.
#ifndef BUSTER_9700X_LIFECYCLE_SUPERSESSION_TEST_H
#define BUSTER_9700X_LIFECYCLE_SUPERSESSION_TEST_H
BUSTER_GLOBAL_LOCAL const char lcn_data_0[] = "{\"id\":38297977458,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 38297937496.1 head a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:29:21Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_1[] = "{\"id\":38297937496,\"run_attempt\":1,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"success\",\"display_title\":\"Merge pull request #3833 from buster14a/claude/relaxed-babbage-6d49np\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:28:59Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_2[] = "{\"total_count\":1,\"check_runs\":[{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"}}]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_3[] = "{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"}}";
BUSTER_GLOBAL_LOCAL const char lcn_data_4[] = "{\"total_count\":0,\"jobs\":[]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_5[] = "\nconcurrency:\n  group: buster-9700x-direct-bench-${{ github.event.workflow_run.event == 'push' && 'main-compare' || github.event.workflow_run.head_branch }}\n  cancel-in-progress: ${{ github.event.workflow_run.event != 'push' }}\n\njobs:\n";
BUSTER_GLOBAL_LOCAL const char lcn_data_6[] = "<!DOCTYPE html><html><a href=\"/buster14a/buster/actions/runs/38297977458/workflow\"><strong>9700X direct workload benchmark</strong></a><div class=\"color-fg-muted\"><div data-target=\"annotation-message.annotationContainer\" class=\"text-small text-mono annotation--contracted\">\n  <div>Canceling since a higher priority waiting request for buster-9700x-direct-bench-main-compare exists</div>\n</div></div></html>";
BUSTER_GLOBAL_LOCAL const char lcn_data_7[] = "{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"},\"conclusion\":\"neutral\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_8[] = "<!DOCTYPE html><html><a href=\"/buster14a/buster/actions/runs/38297977458/workflow\"><strong>9700X direct workload benchmark</strong></a><div class=\"color-fg-muted\"><div data-target=\"annotation-message.annotationContainer\" class=\"text-small text-mono annotation--contracted\">\n  <div>The workflow was cancelled by davidgmbb</div>\n</div></div></html>";
BUSTER_GLOBAL_LOCAL const char lcn_data_9[] = "{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"},\"conclusion\":\"cancelled\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_10[] = "<!DOCTYPE html><html><a href=\"/buster14a/buster/actions/runs/38297977458/workflow\"><strong>9700X direct workload benchmark</strong></a><div class=\"color-fg-muted\"><div data-target=\"annotation-message.annotationContainer\" class=\"text-small text-mono annotation--contracted\">\n  <div>No newer pending request is recorded</div>\n</div></div></html>";
BUSTER_GLOBAL_LOCAL const char lcn_data_11[] = "{\"total_count\":1,\"jobs\":[{\"id\":5,\"name\":\"Authorize the main commit comparison\",\"started_at\":\"2026-10-12T19:29:22Z\"}]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_12[] = "{\"jobs\":[]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_13[] = "{\"total_count\":null,\"jobs\":[]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_14[] = "{\"total_count\":\"0\",\"jobs\":[]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_15[] = "{\"total_count\":101,\"jobs\":[]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_16[] = "{\"total_count\":0,\"jobs\":[{\"id\":5}]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_17[] = "{broken";
BUSTER_GLOBAL_LOCAL const char lcn_data_18[] = "{\"total_count\":0,\"total_count\":0,\"jobs\":[]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_19[] = "{\"total_count\":0,\"jobs\":null}";
BUSTER_GLOBAL_LOCAL const char lcn_data_20[] = "{}";
BUSTER_GLOBAL_LOCAL const char lcn_data_21[] = "<!DOCTYPE html><html><a href=\"/buster14a/buster/actions/runs/38297977458/workflow\"><strong>9700X direct workload benchmark</strong></a><div class=\"color-fg-muted\"><div data-target=\"annotation-message.annotationContainer\" class=\"text-small text-mono annotation--contracted\">\n  <div>Canceling since a higher priority waiting request for foreign-group exists</div>\n</div></div></html>";
BUSTER_GLOBAL_LOCAL const char lcn_data_22[] = "<!DOCTYPE html><html><a href=\"/buster14a/buster/actions/runs/999/workflow\"><strong>9700X direct workload benchmark</strong></a><div class=\"color-fg-muted\"><div data-target=\"annotation-message.annotationContainer\" class=\"text-small text-mono annotation--contracted\">\n  <div>Canceling since a higher priority waiting request for buster-9700x-direct-bench-main-compare exists</div>\n</div></div></html>";
BUSTER_GLOBAL_LOCAL const char lcn_data_23[] = "<!DOCTYPE html><html>Canceling since a higher priority waiting request for buster-9700x-direct-bench-main-compare exists</html>";
BUSTER_GLOBAL_LOCAL const char lcn_data_24[] = "\nconcurrency:\n  group: buster-9700x-direct-bench-${{ github.event.workflow_run.event == 'push' && 'main-compare' || github.event.workflow_run.head_branch }}\n  cancel-in-progress: ${{ github.event.workflow_run.event == 'push' }}\n\njobs:\n";
BUSTER_GLOBAL_LOCAL const char lcn_data_25[] = "{\"id\":38297977458,\"run_attempt\":2,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 38297937496.1 head a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:29:21Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_26[] = "{\"id\":38297937496,\"run_attempt\":2,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"success\",\"display_title\":\"Merge pull request #3833 from buster14a/claude/relaxed-babbage-6d49np\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:28:59Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_27[] = "{\"id\":38297977458,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"failure\",\"display_title\":\"9700X request 38297937496.1 head a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:29:21Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_28[] = "{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"},\"conclusion\":\"failure\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_29[] = "{\"id\":38297977458,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"timed_out\",\"display_title\":\"9700X request 38297937496.1 head a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:29:21Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_30[] = "{\"id\":38297977458,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"success\",\"display_title\":\"9700X request 38297937496.1 head a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:29:21Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_31[] = "{\"id\":38297937496,\"run_attempt\":1,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"failure\",\"display_title\":\"Merge pull request #3833 from buster14a/claude/relaxed-babbage-6d49np\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:28:59Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_32[] = "{\"id\":38297937496,\"run_attempt\":1,\"path\":\".github/workflows/9700x-compiler-request.yml\",\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"Merge pull request #3833 from buster14a/claude/relaxed-babbage-6d49np\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:28:59Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_33[] = "{\"total_count\":1,\"check_runs\":[{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.2:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"}}]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_34[] = "{\"total_count\":1,\"check_runs\":[{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"},\"conclusion\":\"neutral\"}]}";
BUSTER_GLOBAL_LOCAL const char lcn_data_35[] = "{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"completed\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: terminal-native-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"},\"conclusion\":\"success\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_36[] = "{\"id\":38297977458,\"run_attempt\":1,\"path\":\".github/workflows/9700x-direct-bench.yml\",\"event\":\"workflow_run\",\"head_branch\":\"main\",\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"status\":\"completed\",\"conclusion\":\"cancelled\",\"display_title\":\"9700X request 38297937496.1 head a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"repository\":{\"full_name\":\"buster14a/buster\",\"id\":1},\"head_repository\":{\"full_name\":\"buster14a/buster\",\"id\":1071732997},\"created_at\":\"2026-10-12T19:29:21Z\"}";
BUSTER_GLOBAL_LOCAL const char lcn_data_37[] = "{\"id\":114973570744,\"head_sha\":\"a4ea18116d1dd5d03ecf7287234fe61f4fb40267\",\"name\":\"9700X compiler benchmark\",\"external_id\":\"buster-9700x-compiler-main-v1:a4ea18116d1dd5d03ecf7287234fe61f4fb40267:38297937496.1:1\",\"status\":\"queued\",\"app\":{\"id\":15368},\"details_url\":\"https://github.com/buster14a/buster/runs/114973570744\",\"output\":{\"summary\":\"**9700X compiler benchmark: queued** (report-only; it never blocks merging)\\n\\nLifecycle protocol: foreign-v1.\\nCandidate `a4ea18116d1dd5d03ecf7287234fe61f4fb40267`.\\nBaseline: its first parent, the main commit it landed on, or, when that commit has no valid measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile `compiler-compare-v1`.\\nRequest run 38297937496 attempt 1: https://github.com/buster14a/buster/actions/runs/38297937496/attempts/1\\n\\nWaiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub keeps only the newest pending main commit, so this commit may be displaced and is then marked **Not measured**. The comparison run will appear under https://github.com/buster14a/buster/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run.\"}}";
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_0[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_6, 1, 0},
    {"actions/runs/38297977458", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496", "GET", lcn_data_1, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_7, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_1[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_8, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_2[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_10, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_3[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_11, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_4[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_12, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_5[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_13, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_6[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_14, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_7[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_15, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_8[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_16, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_9[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_17, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_10[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_18, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_11[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_19, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_12[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_20, 0, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_20, 0, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_20, 0, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_13[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_6, 0, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_14[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_21, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_15[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_22, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_16[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_23, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_17[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_24, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_18[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_6, 1, 0},
    {"actions/runs/38297977458", "GET", lcn_data_25, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_19[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
    {"contents/.github/workflows/9700x-direct-bench.yml?ref=a4ea18116d1dd5d03ecf7287234fe61f4fb40267", "GET", lcn_data_5, 1, 0},
    {"https://github.com/buster14a/buster/actions/runs/38297977458/attempts/1", "GET", lcn_data_6, 1, 0},
    {"actions/runs/38297977458", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496", "GET", lcn_data_26, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_20[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_27, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_28, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_21[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_29, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_28, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_22[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_30, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_28, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_23[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_31, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_28, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_24[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_32, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_3, 1, 0},
    {"check-runs/114973570744", "PATCH", lcn_data_9, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_25[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_33, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_26[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_34, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_27[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_35, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_28[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_36, 1, 0},
};
BUSTER_GLOBAL_LOCAL const CmResponse lcn_case_29[] =
{
    {"actions/runs/38297977458/attempts/1", "GET", lcn_data_0, 1, 0},
    {"actions/runs/38297937496/attempts/1", "GET", lcn_data_1, 1, 0},
    {"commits/a4ea18116d1dd5d03ecf7287234fe61f4fb40267/check-runs?filter=all&app_id=15368&per_page=100&page=1", "GET", lcn_data_2, 1, 0},
    {"check-runs/114973570744", "GET", lcn_data_37, 1, 0},
    {"actions/runs/38297977458/attempts/1/jobs?per_page=100", "GET", lcn_data_4, 1, 0},
};
BUSTER_GLOBAL_LOCAL int lcn_test(unsigned *count)
{
    const LcTestCase cases[] =
    {
        {"incident pending replacement is neutral", lcn_case_0, 11, 38297977458, 1, 1, 1, 0, 0, 0},
        {"manual or unknown cancellation preserves cancelled", lcn_case_1, 9, 38297977458, 1, 1, 1, 0, 0, 0},
        {"no successor cause is not asserted", lcn_case_2, 9, 38297977458, 1, 1, 1, 0, 0, 0},
        {"execution began even with supersession annotation", lcn_case_3, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"missing count does not become neutral", lcn_case_4, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"null count does not become neutral", lcn_case_5, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"string count does not become neutral", lcn_case_6, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"incomplete pagination does not become neutral", lcn_case_7, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"inconsistent inventory does not become neutral", lcn_case_8, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"malformed jobs does not become neutral", lcn_case_9, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"duplicate count does not become neutral", lcn_case_10, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"null array does not become neutral", lcn_case_11, 7, 38297977458, 1, 1, 1, 0, 0, 0},
        {"unavailable jobs does not become neutral", lcn_case_12, 9, 38297977458, 1, 1, 1, 0, 0, 0},
        {"unavailable annotation does not become neutral", lcn_case_13, 9, 38297977458, 1, 1, 1, 0, 0, 0},
        {"wrong pending group does not become neutral", lcn_case_14, 9, 38297977458, 1, 1, 1, 0, 0, 0},
        {"annotation bound to foreign executor is rejected", lcn_case_15, 9, 38297977458, 1, 1, 1, 0, 0, 0},
        {"plain message outside annotation cannot authorize neutral", lcn_case_16, 9, 38297977458, 1, 1, 1, 0, 0, 0},
        {"changed pinned concurrency policy preserves cancellation", lcn_case_17, 8, 38297977458, 1, 1, 1, 0, 0, 0},
        {"changed executor attempt preserves cancellation", lcn_case_18, 10, 38297977458, 1, 1, 1, 0, 0, 0},
        {"changed request attempt preserves cancellation", lcn_case_19, 11, 38297977458, 1, 1, 1, 0, 0, 0},
        {"failure executor is not neutral", lcn_case_20, 6, 38297977458, 1, 1, 1, 0, 0, 0},
        {"timed_out executor is not neutral", lcn_case_21, 6, 38297977458, 1, 1, 1, 0, 0, 0},
        {"success executor is not neutral", lcn_case_22, 6, 38297977458, 1, 1, 1, 0, 0, 0},
        {"failure request is not neutral", lcn_case_23, 6, 38297977458, 1, 1, 1, 0, 0, 0},
        {"cancelled request is not neutral", lcn_case_24, 6, 38297977458, 1, 1, 1, 0, 0, 0},
        {"foreign check attempt is untouched", lcn_case_25, 4, 38297977458, 1, 1, 0, 0, 0, 1},
        {"terminal neutral duplicate stays immutable", lcn_case_26, 4, 38297977458, 1, 1, 0, 1, 0, 0},
        {"fresh terminal measurement stays immutable", lcn_case_27, 5, 38297977458, 1, 1, 0, 1, 0, 0},
        {"foreign repository executor cannot publish", lcn_case_28, 1, 38297977458, 1, 0, 0, 0, 0, 0},
        {"foreign protocol does not authorize a write", lcn_case_29, 5, 38297977458, 1, 1, 0, 0, 1, 0},
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
            transport.fixture_cursor == cases[i].count && transport.requests <= 13;
        failures += !pass;
        if (i < 25)
        {
            // Independently assert the computed verdict; transport fixtures
            // cannot make a neutral PATCH response stand in for its request body.
            CmTransport probe = {0}; probe.deadline = cm_clock() + 180; probe.request_limit = 60;
            probe.fixture = cases[i].fixture; probe.fixture_count = cases[i].count;
            LcIdentity pending = {0};
            int bound = lc_bind(&probe, cases[i].run, cases[i].attempt, &pending);
            probe.fixture_cursor = 4;
            CmJson queued = cm_json_parse(cases[i].fixture[3].content, strlen(cases[i].fixture[3].content));
            pending.superseded = bound && lc_pending_superseded(&probe, &pending,
                cm_get(&queued, cm_member(&queued, 1, "output"), "summary"));
            int verdict = pending.superseded == (i == 0) &&
                cm_equal(lc_conclusion(&pending), i == 0 ? "neutral" :
                    i == 20 || i == 21 || i == 22 || i == 23 ? "failure" : "cancelled");
            failures += !verdict;
            printf("{\"fixture\":\"independent pending verdict %u\",\"status\":\"%s\"}\n", i, verdict ? "pass" : "fail");
            cm_json_free(&queued);
        }
        printf("{\"fixture\":"); cm_quote(stdout, cases[i].label);
        printf(",\"status\":\"%s\",\"valid\":%d,\"closed\":%u,\"terminal\":%u,\"foreign\":%u,\"unavailable\":%u,\"requests\":%u,\"consumed\":%u,\"expected\":%u}\n",
            pass ? "pass" : "fail", valid, result.closed, result.terminal, result.foreign, result.unavailable,
            transport.requests, transport.fixture_cursor, cases[i].count);
    }
    LcIdentity id = {0};
    id.executor = 38297977458; id.attempt = 1; id.request = 38297937496; id.request_attempt = 1;
    cm_copy(id.head, sizeof(id.head), "a4ea18116d1dd5d03ecf7287234fe61f4fb40267");
    cm_copy(id.trusted, sizeof(id.trusted), id.head); cm_copy(id.outcome, sizeof(id.outcome), "cancelled");
    lc_marker(&id); id.superseded = 1;
    const char *prior = "**9700X compiler benchmark: queued**\n\n" LC_NATIVE "\nBaseline: unavailable.\n";
    char *body = lc_body(&id, prior, 0);
    CmJson report = cm_json_parse(body ? body : "", body ? strlen(body) : 0);
    const char *summary = cm_get(&report, cm_member(&report, 1, "output"), "summary");
    int rendered = report.valid && cm_equal(cm_get(&report, 1, "conclusion"), "neutral") &&
        cm_equal(cm_get(&report, cm_member(&report, 1, "output"), "title"), "Not measured: superseded before execution") &&
        strncmp(summary, "**9700X compiler benchmark: not measured; superseded before execution.**", 72) == 0 &&
        strstr(summary, "native executor remains cancelled") && strstr(summary, "contains zero jobs") &&
        strstr(summary, "Baseline: unavailable.") && strstr(summary, id.head) && strstr(summary, id.details) &&
        strstr(summary, "Request run 38297937496 attempt 1:") && strstr(summary, "Workflow run 38297977458 attempt 1:") &&
        strstr(summary, "Prior report (historical; not current execution state):\n\n> **9700X compiler benchmark: queued**");
    failures += !rendered;
    printf("{\"fixture\":\"neutral report has one current terminal heading and exact provenance\",\"status\":\"%s\"}\n", rendered ? "pass" : "fail");
    cm_json_free(&report); free(body);
    body = lc_body(&id, "Baseline `0123456789012345678901234567890123456789` (first parent).\n", 0);
    report = cm_json_parse(body ? body : "", body ? strlen(body) : 0);
    summary = cm_get(&report, cm_member(&report, 1, "output"), "summary");
    failures += !report.valid || !lc_line(summary, "Baseline `0123456789012345678901234567890123456789` (first parent).");
    cm_json_free(&report); free(body);
    body = lc_body(&id, "Baseline: its first parent (selection policy, no known SHA).\n", 0);
    report = cm_json_parse(body ? body : "", body ? strlen(body) : 0);
    summary = cm_get(&report, cm_member(&report, 1, "output"), "summary");
    failures += !report.valid || !lc_line(summary, "Baseline: unavailable.");
    cm_json_free(&report); free(body);
    failures += lc_pending_annotation("<!DOCTYPE html><html>unavailable</html>", id.executor);
    failures += lc_pending_annotation("malformed", id.executor);
    failures += lc_pending_annotation(NULL, id.executor);
    failures += lc_pending_annotation("<a href=\"/buster14a/buster/actions/runs/38297977458/workflow\"><strong>9700X direct workload benchmark</strong></a><div class=\"color-fg-muted\"><div data-target=\"annotation-message.annotationContainer\" class=\"text-small text-mono annotation--contracted\">\n  <div>Canceling since a higher priority waiting request for buster-9700x-direct-bench-main-compare exists</div>\n</div></div><!DOCTYPE html><html><a href=\"/buster14a/buster/actions/runs/38297977458/workflow\"><strong>9700X direct workload benchmark</strong></a><div class=\"color-fg-muted\"><div data-target=\"annotation-message.annotationContainer\" class=\"text-small text-mono annotation--contracted\">\n  <div>Canceling since a higher priority waiting request for buster-9700x-direct-bench-main-compare exists</div>\n</div></div></html>", id.executor);
    // Even a copied flag cannot neutralize PR/research/request-only outcomes.
    id.pull = 1; failures += !cm_equal(lc_conclusion(&id), "cancelled"); id.pull = 0;
    id.research = LC_RESEARCH_SAMPLING; failures += !cm_equal(lc_conclusion(&id), "cancelled"); id.research = 0;
    id.request_only = 1; failures += !cm_equal(lc_conclusion(&id), "cancelled"); id.request_only = 0;
    id.attempt = 2; failures += !cm_equal(lc_conclusion(&id), "cancelled"); id.attempt = 1;
    cm_copy(id.outcome, sizeof(id.outcome), "failure"); failures += !cm_equal(lc_conclusion(&id), "failure");
    *count = (unsigned)BUSTER_ARRAY_LENGTH(cases) + 37;
    int result = (int)failures;
    return result;
}
#endif
