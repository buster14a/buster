// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Deterministic calendar-observer fixtures, included by nightly.c after its shared test helpers.
// These assert the daily full-measurement contract, not the coordinator's admission decisions.

#define BUSTER_NIGHTLY_OBSERVER_TEST_ATTEMPTS 8
#define BUSTER_NIGHTLY_OBSERVER_TEST_CYCLES 4

typedef struct NightlyObserverFixture NightlyObserverFixture;
struct NightlyObserverFixture
{
    NightlyLedger ledger;
    NightlyObserverInput input;
    NightlyCycle cycles[BUSTER_NIGHTLY_OBSERVER_TEST_CYCLES];
    NightlyRow rows[BUSTER_NIGHTLY_OBSERVER_TEST_ATTEMPTS];
    NightlyObserverAttempt attempts[BUSTER_NIGHTLY_OBSERVER_TEST_ATTEMPTS];
    NightlyRecord records[BUSTER_NIGHTLY_OBSERVER_TEST_ATTEMPTS];
    char since[11];
};

static void nightly_observer_test_fresh(NightlyObserverFixture* fixture, int64_t now)
{
    fixture->input.now = now;
    fixture->input.collected_start = now - 60;
    fixture->input.collected_end = now;
    fixture->input.ledger_fetched = now - 30;
    fixture->input.ledger_published = now - 120;
}

static void nightly_observer_test_pin(NightlyObserverFixture* fixture, uint32_t index, int year, int month, int day, uint32_t sha)
{
    NightlyCycle* cycle = &fixture->cycles[index];
    snprintf(cycle->date, sizeof(cycle->date), "%04d-%02d-%02d", year, month, day);
    nightly_test_sha(sha, cycle->snapshot);
    cycle->coordinator = 9000;
    cycle->pinned = nightly_test_madrid(year, month, day - 1, 22, 7);
    fixture->ledger.cycle_count = index + 1;
}

static void nightly_observer_test_reset(NightlyObserverFixture* fixture, int year, int month, int day)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ledger.rows = fixture->rows;
    fixture->ledger.cycles = fixture->cycles;
    fixture->input.ledger = &fixture->ledger;
    fixture->input.attempts = fixture->attempts;
    fixture->input.since = fixture->since;
    fixture->input.collection_complete = true;
    fixture->input.ledger_present = true;
    snprintf(fixture->since, sizeof(fixture->since), "%04d-%02d-%02d", year, month, day);
    nightly_observer_test_fresh(fixture, nightly_test_madrid(year, month, day, 11, 0));
    nightly_observer_test_pin(fixture, 0, year, month, day, 1);
}

static void nightly_observer_test_attempt(NightlyObserverFixture* fixture, uint32_t index, uint32_t cycle_index, int year, int month, int day, uint64_t run, uint32_t attempt)
{
    const NightlyCycle* pin = &fixture->cycles[cycle_index];
    NightlyObserverAttempt* item = &fixture->attempts[index];
    NightlyRecord* record = &fixture->records[index];
    char pinned_sha[41];
    nightly_copy(pinned_sha, sizeof(pinned_sha), pin->snapshot);
    char attempt_text[12];
    snprintf(attempt_text, sizeof(attempt_text), "%u", attempt);
    item->run.id = run;
    item->run.attempt = attempt;
    item->run.event = NIGHTLY_EVENT_DISPATCH;
    item->run.completed = true;
    item->run.conclusion = NIGHTLY_CONCLUSION_SUCCESS;
    item->run.workflow = true;
    item->run.repository = BUSTER_NIGHTLY_REPOSITORY;
    item->run.nightly = true;
    item->run.purpose = NIGHTLY_PURPOSE_SNAPSHOT;
    item->run.admit = true;
    item->run.created = nightly_test_madrid(year, month, day, 3, 0);
    const int64_t rerun_delay = (int64_t)(attempt - 1) * 2 * 3600;
    item->run.updated = nightly_test_madrid(year, month, day, 6, 0) + rerun_delay;
    nightly_copy(item->run.head, sizeof(item->run.head), pin->snapshot);
    nightly_copy(item->run.target, sizeof(item->run.target), pin->snapshot);
    snprintf(item->run.branch, sizeof(item->run.branch), "nightly/%s", pinned_sha);
    snprintf(item->run.request, sizeof(item->run.request), "%04d%02d%02d.9000.%llu.admit", year, month, day, (unsigned long long)run);
    item->started = item->run.created + rerun_delay;
    item->job = run * 10 + attempt;
    item->hardware_started = nightly_test_madrid(year, month, day, 4, 0) + rerun_delay;
    item->hardware_finished = nightly_test_madrid(year, month, day, 5, 0) + rerun_delay;
    item->facts_valid = true;
    item->jobs_complete = true;
    item->hardware_conclusion = NIGHTLY_CONCLUSION_SUCCESS;
    item->hardware_completed = true;
    item->hardware_present = true;
    item->record = record;
    item->record_state = NIGHTLY_OBSERVER_RECORD_PRESENT;
    item->published = item->run.updated;
    item->worker_start = 2;
    item->worker_end = 8;
    nightly_test_record(record, run, "workflow_dispatch", "snapshot", pin->snapshot, pin->snapshot, pin->snapshot, "-");
    nightly_copy(record->values[NIGHTLY_FIELD_ATTEMPT], BUSTER_NIGHTLY_VALUE, attempt_text);
    nightly_copy(record->values[NIGHTLY_FIELD_REQUEST], BUSTER_NIGHTLY_VALUE, item->run.request);
    nightly_copy(record->values[NIGHTLY_FIELD_SNAPSHOT], BUSTER_NIGHTLY_VALUE, pin->snapshot);
    nightly_copy(record->values[NIGHTLY_FIELD_MODE], BUSTER_NIGHTLY_VALUE, "on");
    nightly_copy(record->values[NIGHTLY_FIELD_MEASURE], BUSTER_NIGHTLY_VALUE, "all");
    nightly_copy(record->values[NIGHTLY_FIELD_COVERAGE], BUSTER_NIGHTLY_VALUE, "full");
    nightly_copy(record->values[NIGHTLY_FIELD_WINDOW], BUSTER_NIGHTLY_VALUE, "open");
    NightlyRow* row = &fixture->rows[index];
    row->run = run;
    row->attempt = attempt;
    row->event = NIGHTLY_EVENT_DISPATCH;
    row->purpose = NIGHTLY_PURPOSE_SNAPSHOT;
    row->created = item->run.created;
    row->completed = item->run.updated;
    row->plain = NIGHTLY_PLAIN_OK;
    row->hardware = NIGHTLY_HARDWARE_ADMIT;
    row->coverage = NIGHTLY_COVERAGE_FULL;
    row->outcome = NIGHTLY_OUTCOME_COMPLETE;
    nightly_copy(row->commit, sizeof(row->commit), pin->snapshot);
    nightly_copy(row->request, sizeof(row->request), item->run.request);
    fixture->input.attempt_count = index + 1;
    fixture->ledger.row_count = index + 1;
}

static char* nightly_observer_test_report(const NightlyObserverInput* input, int* result)
{
    char* data = NULL;
    size_t length = 0;
    FILE* stream = open_memstream(&data, &length);
    if (stream == NULL)
    {
        nightly_fatal("observer self-test report stream", NULL);
    }
    *result = nightly_observe_report(input, stream);
    if (fclose(stream) != 0)
    {
        nightly_fatal("observer self-test report close", NULL);
    }
    return data;
}

static void nightly_observer_test_status(NightlyTest* test, const NightlyObserverFixture* fixture, const char* expected, const char* message)
{
    const NightlyObserverCycle cycle = nightly_observe_cycle(&fixture->input, fixture->since);
    nightly_expect(test, strcmp(cycle.status, expected) == 0, message);
}

static void nightly_test_observe(NightlyTest* test)
{
    NightlyObserverFixture* fixture = nightly_allocate(1, sizeof(NightlyObserverFixture));
    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: full native evidence completes at the calendar pin");
    int result = 0;
    char* report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 0 && strstr(report, fixture->cycles[0].snapshot) != NULL && strstr(report, "/runs/1001/attempts/1") != NULL, "observer: the passing report retains the exact pin and attempt URL");
    nightly_expect(test, strstr(report, "full") != NULL && strstr(report, "passed") != NULL, "observer: declared full coverage and native qualification remain visible");
    free(report);
    fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_FAILURE;
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: a proven full native measurement is independent of unrelated later workflow failure");
    fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_SUCCESS;
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_OVERRIDE], BUSTER_NIGHTLY_VALUE, "bounded same-window retry");
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: an operator reason preserves valid proof inside the original hardware window");
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 0 && strstr(report, "bounded same-window retry") != NULL, "observer: the operator's recorded reason remains visible");
    free(report);
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_OVERRIDE], BUSTER_NIGHTLY_VALUE, "-");

    // Snapshot dispatches bypass the selector. The native worker's fixed all-inventory policy
    // is represented by measure=- only when the changes job was actually skipped.
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_MEASURE], BUSTER_NIGHTLY_VALUE, "-");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_CHANGES_RESULT], BUSTER_NIGHTLY_VALUE, "skipped");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_CODE], BUSTER_NIGHTLY_VALUE, "-");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_HOSTED], BUSTER_NIGHTLY_VALUE, "reused");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_HOSTED_SOURCE], BUSTER_NIGHTLY_VALUE, "900");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_TEST_RESULT], BUSTER_NIGHTLY_VALUE, "skipped");
    nightly_expect(test, nightly_test_reparse(&fixture->records[0], &fixture->records[7]), "observer: the production snapshot fixture follows the coverage-record schema");
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: a production snapshot's skipped selector retains its fixed full native inventory");
    static const char* const changes_ran[] = { "success", "failure", "cancelled" };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(changes_ran); i += 1)
    {
        nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_CHANGES_RESULT], BUSTER_NIGHTLY_VALUE, changes_ran[i]);
        const NightlyObserverCycle cycle = nightly_observe_cycle(&fixture->input, fixture->since);
        nightly_expect(test, strncmp(cycle.status, "complete-", 9) != 0, "observer: missing measure with a non-skipped selector does not prove the full inventory");
    }
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_MEASURE], BUSTER_NIGHTLY_VALUE, "all");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_CHANGES_RESULT], BUSTER_NIGHTLY_VALUE, "success");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_CODE], BUSTER_NIGHTLY_VALUE, "true");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_HOSTED], BUSTER_NIGHTLY_VALUE, "run");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_HOSTED_SOURCE], BUSTER_NIGHTLY_VALUE, "-");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_TEST_RESULT], BUSTER_NIGHTLY_VALUE, "success");

    // A substitute retains the original pin and proves its own measurement, not another cycle's record.
    char substitute[41];
    nightly_test_sha(2, substitute);
    nightly_copy(fixture->attempts[0].run.head, sizeof(fixture->attempts[0].run.head), substitute);
    nightly_copy(fixture->attempts[0].run.target, sizeof(fixture->attempts[0].run.target), substitute);
    snprintf(fixture->attempts[0].run.branch, sizeof(fixture->attempts[0].run.branch), "nightly/%s", substitute);
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_COMMIT], BUSTER_NIGHTLY_VALUE, substitute);
    nightly_copy(fixture->rows[0].commit, sizeof(fixture->rows[0].commit), substitute);
    nightly_observer_test_status(test, fixture, "complete-substitute", "observer: a proven substitute is distinguished from completion at the original pin");
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 0 && strstr(report, substitute) != NULL && strstr(report, fixture->cycles[0].snapshot) != NULL, "observer: both the original pin and substitute measurement SHA survive in the report");
    free(report);

    // Every completion precondition matters independently. The baseline deliberately has scope=plain:
    // admitted snapshots may reuse hosted preparation while measuring the full native inventory.
    static const struct
    {
        NightlyField field;
        const char* value;
    } denied[] = {
        { NIGHTLY_FIELD_COVERAGE, "targeted" },
        { NIGHTLY_FIELD_COVERAGE, "plain" },
        { NIGHTLY_FIELD_MEASURE, ",lexer," },
        { NIGHTLY_FIELD_NATIVE, "started" },
        { NIGHTLY_FIELD_NATIVE_EVIDENCE, "smoke" },
        { NIGHTLY_FIELD_NATIVE_EVIDENCE, "reused" },
        { NIGHTLY_FIELD_MEASUREMENT_EVIDENCE, "reused" },
        { NIGHTLY_FIELD_HARDWARE, "skip" },
        { NIGHTLY_FIELD_BENCHMARK_RESULT, "skipped" },
        { NIGHTLY_FIELD_WINDOW, "closed" },
        { NIGHTLY_FIELD_ATTEMPT, "2" },
        { NIGHTLY_FIELD_RUN, "1002" },
        { NIGHTLY_FIELD_EVENT, "push" },
        { NIGHTLY_FIELD_PURPOSE, "backlog" },
        { NIGHTLY_FIELD_OUTCOME, "deferred" },
        { NIGHTLY_FIELD_REQUEST, "20261125.9000.1001.admit" },
        { NIGHTLY_FIELD_SNAPSHOT, "0000000000000000000000000000000000000000" },
        { NIGHTLY_FIELD_COMMIT, "0000000000000000000000000000000000000000" },
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(denied); i += 1)
    {
        nightly_observer_test_reset(fixture, 2026, 11, 26);
        nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
        nightly_copy(fixture->records[0].values[denied[i].field], BUSTER_NIGHTLY_VALUE, denied[i].value);
        const NightlyObserverCycle cycle = nightly_observe_cycle(&fixture->input, fixture->since);
        char message[256];
        snprintf(message, sizeof(message), "observer: %s=%s cannot supply complete native/full evidence", nightly_record_keys[denied[i].field].name, denied[i].value);
        nightly_expect(test, strncmp(cycle.status, "complete-", 9) != 0, message);
    }

    // An entire date missing between complete dates cannot disappear with the planner's noon rollover.
    nightly_observer_test_reset(fixture, 2026, 11, 24);
    nightly_observer_test_pin(fixture, 1, 2026, 11, 26, 3);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 24, 1001, 1);
    nightly_observer_test_attempt(fixture, 1, 1, 2026, 11, 26, 1002, 1);
    nightly_observer_test_fresh(fixture, nightly_test_madrid(2026, 11, 26, 14, 0));
    const NightlyObserverCycle missing_date = nightly_observe_cycle(&fixture->input, "2026-11-25");
    nightly_expect(test, strcmp(missing_date.status, "not-attempted") == 0 && missing_date.pin == NULL && missing_date.latest == NULL, "observer: a whole lost date has its own not-attempted result without a fabricated pin");
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 1 && strstr(report, "2026-11-24") != NULL && strstr(report, "2026-11-25") != NULL && strstr(report, "2026-11-26") != NULL, "observer: the closed civil-date cohort includes the missing middle date after noon");
    free(report);

    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_status(test, fixture, "not-attempted", "observer: a pin alone does not imply an admission");
    fixture->ledger.cycle_count = 0;
    nightly_observer_test_status(test, fixture, "not-attempted", "observer: an observed empty ledger still exposes a missed closed date");
    fixture->input.ledger_present = false;
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 2 && strstr(report, "2026-11-26") != NULL, "observer: unavailable ledger publication reports the expected date without a pass");
    free(report);

    // Early selector/hosted failures and interrupted hardware remain separate from missing publication.
    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
    fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_FAILURE;
    fixture->attempts[0].hardware_present = false;
    fixture->attempts[0].hardware_completed = false;
    fixture->attempts[0].record = NULL;
    fixture->attempts[0].record_state = NIGHTLY_OBSERVER_RECORD_MISSING;
    fixture->attempts[0].published = 0;
    fixture->rows[0].outcome = NIGHTLY_OUTCOME_REJECTED;
    nightly_observer_test_status(test, fixture, "failed", "observer: early failure before hardware or record publication is still an observed failed admission");
    fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_CANCELLED;
    fixture->rows[0].outcome = NIGHTLY_OUTCOME_INTERRUPTED;
    nightly_observer_test_status(test, fixture, "interrupted", "observer: cancellation without a coverage record remains interrupted");
    fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_TIMED_OUT;
    nightly_observer_test_status(test, fixture, "interrupted", "observer: timeout without a coverage record remains interrupted");

    // The harvester may have frozen a missing-record fallback; a late artifact supplies new evidence
    // without rewriting that row, and the native deadline remains independent of publication time.
    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
    fixture->attempts[0].record = NULL;
    fixture->attempts[0].record_state = NIGHTLY_OBSERVER_RECORD_MISSING;
    fixture->attempts[0].published = 0;
    fixture->rows[0].outcome = NIGHTLY_OUTCOME_FAILED;
    nightly_observer_test_status(test, fixture, "awaiting-evidence", "observer: a green native job and missing record cannot certify full coverage");
    nightly_observer_test_fresh(fixture, nightly_test_madrid(2026, 11, 26, 8, 30));
    NightlyObserverCycle waiting = nightly_observe_cycle(&fixture->input, fixture->since);
    nightly_expect(test, waiting.within_grace && waiting.deadline == nightly_test_madrid(2026, 11, 26, 8, 0) && waiting.report_deadline == nightly_test_madrid(2026, 11, 26, 10, 0), "observer: the 08:00 hardware deadline and 10:00 reporting grace are distinct");
    nightly_observer_test_fresh(fixture, nightly_test_madrid(2026, 11, 26, 11, 0));
    fixture->attempts[0].record = &fixture->records[0];
    fixture->attempts[0].record_state = NIGHTLY_OBSERVER_RECORD_PRESENT;
    fixture->attempts[0].published = nightly_test_madrid(2026, 11, 26, 10, 30);
    fixture->attempts[0].run.updated = fixture->attempts[0].published;
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: late publication resolves an on-time full measurement despite a frozen fallback ledger row");
    nightly_expect(test, fixture->rows[0].outcome == NIGHTLY_OUTCOME_FAILED, "observer: observation preserves the original fallback ledger row");
    fixture->attempts[0].hardware_finished = nightly_test_madrid(2026, 11, 26, 8, 0) + 1;
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_OVERRIDE], BUSTER_NIGHTLY_VALUE, "operator-recovery");
    const NightlyObserverCycle late_hardware = nightly_observe_cycle(&fixture->input, fixture->since);
    nightly_expect(test, strncmp(late_hardware.status, "complete-", 9) != 0, "observer: reporting grace never widens the hardware completion deadline");
    fixture->attempts[0].hardware_finished -= 1;
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: hardware finishing exactly at its deadline is on time");

    for (uint32_t i = 0; i < 4; i += 1)
    {
        nightly_observer_test_reset(fixture, 2026, 11, 26);
        nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
        if (i == 0)
        {
            fixture->cycles[0].pinned = fixture->attempts[0].run.created + 1;
        }
        else if (i == 1)
        {
            fixture->attempts[0].run.updated = fixture->input.now + 1;
        }
        else if (i == 2)
        {
            fixture->attempts[0].run.updated = fixture->attempts[0].run.created - 1;
        }
        else
        {
            fixture->attempts[0].started = fixture->attempts[0].run.created - 1;
        }
        const NightlyObserverCycle cycle = nightly_observe_cycle(&fixture->input, fixture->since);
        nightly_expect(test, strncmp(cycle.status, "complete-", 9) != 0, "observer: pin, run and attempt timestamps must describe a possible chronology");
    }

    // A failed snapshot and a later successful targeted run are not a completed full snapshot.
    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
    fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_FAILURE;
    fixture->attempts[0].hardware_conclusion = NIGHTLY_CONCLUSION_FAILURE;
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_NATIVE], BUSTER_NIGHTLY_VALUE, "started");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_BENCHMARK_RESULT], BUSTER_NIGHTLY_VALUE, "failure");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_OUTCOME], BUSTER_NIGHTLY_VALUE, "blocked-native");
    fixture->rows[0].outcome = NIGHTLY_OUTCOME_BLOCKED_NATIVE;
    nightly_observer_test_attempt(fixture, 1, 0, 2026, 11, 26, 1002, 1);
    fixture->attempts[1].run.purpose = NIGHTLY_PURPOSE_BACKLOG;
    fixture->rows[1].purpose = NIGHTLY_PURPOSE_BACKLOG;
    fixture->rows[1].coverage = NIGHTLY_COVERAGE_TARGETED;
    nightly_copy(fixture->records[1].values[NIGHTLY_FIELD_PURPOSE], BUSTER_NIGHTLY_VALUE, "backlog");
    nightly_copy(fixture->records[1].values[NIGHTLY_FIELD_COVERAGE], BUSTER_NIGHTLY_VALUE, "targeted");
    nightly_observer_test_status(test, fixture, "failed", "observer: later targeted success cannot repair a failed daily full snapshot");

    // A rerun has its own facts, record and jobs; order in the input never chooses the winner.
    fixture->input.attempt_count = 1;
    fixture->ledger.row_count = 1;
    nightly_observer_test_attempt(fixture, 1, 0, 2026, 11, 26, 1001, 2);
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: a valid full rerun can complete the cycle while retaining its first failed attempt");
    NightlyObserverCycle rerun = nightly_observe_cycle(&fixture->input, fixture->since);
    nightly_expect(test, rerun.latest != NULL && rerun.latest->run.attempt == 2 && rerun.failures >= 1, "observer: a passing second attempt keeps the original failure count");
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, strstr(report, "/runs/1001/attempts/1") != NULL && strstr(report, "/runs/1001/attempts/2") != NULL, "observer: both rerun attempt identities remain reviewable");
    free(report);
    NightlyObserverAttempt exchanged = fixture->attempts[0];
    fixture->attempts[0] = fixture->attempts[1];
    fixture->attempts[1] = exchanged;
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: shuffled attempts preserve the newest-attempt result");
    fixture->attempts[0].record = NULL;
    fixture->attempts[0].record_state = NIGHTLY_OBSERVER_RECORD_MISSING;
    fixture->attempts[0].published = 0;
    nightly_observer_test_status(test, fixture, "awaiting-evidence", "observer: a new rerun missing its record never inherits another attempt's green verdict");

    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 2);
    fixture->attempts[0].started = fixture->attempts[0].hardware_started + 1;
    const NightlyObserverCycle inherited_job = nightly_observe_cycle(&fixture->input, fixture->since);
    nightly_expect(test, strncmp(inherited_job.status, "complete-", 9) != 0, "observer: a job inherited from an older attempt cannot qualify a rerun");

    // Hosted full preparation is diagnostic, never native measurement completion.
    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
    fixture->attempts[0].run.admit = false;
    nightly_copy(fixture->attempts[0].run.request, sizeof(fixture->attempts[0].run.request), "20261126.9000.1001.defer");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_REQUEST], BUSTER_NIGHTLY_VALUE, fixture->attempts[0].run.request);
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_HARDWARE], BUSTER_NIGHTLY_VALUE, "skip");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_SCOPE], BUSTER_NIGHTLY_VALUE, "full");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_BENCHMARK_RESULT], BUSTER_NIGHTLY_VALUE, "skipped");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_NATIVE], BUSTER_NIGHTLY_VALUE, "-");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_NATIVE_EVIDENCE], BUSTER_NIGHTLY_VALUE, "-");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_MEASUREMENT_EVIDENCE], BUSTER_NIGHTLY_VALUE, "-");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_FUZZ_RESULT], BUSTER_NIGHTLY_VALUE, "success");
    nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_OUTCOME], BUSTER_NIGHTLY_VALUE, "deferred");
    fixture->attempts[0].hardware_conclusion = NIGHTLY_CONCLUSION_SKIPPED;
    fixture->attempts[0].hardware_started = 0;
    fixture->attempts[0].hardware_finished = 0;
    fixture->rows[0].hardware = NIGHTLY_HARDWARE_SKIP;
    fixture->rows[0].outcome = NIGHTLY_OUTCOME_DEFERRED;
    nightly_copy(fixture->rows[0].request, sizeof(fixture->rows[0].request), fixture->attempts[0].run.request);
    nightly_observer_test_status(test, fixture, "not-attempted", "observer: successful full hosted preparation supplies no native completion");
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 1 && strstr(report, "Hosted preparation") != NULL && strstr(report, "Hosted diagnostics") != NULL, "observer: hosted preparation diagnostics are visible separately from the daily native result");
    free(report);

    // Complete, bounded, fresh collection is a prerequisite even when an old source looked green.
    for (uint32_t i = 0; i < 8; i += 1)
    {
        nightly_observer_test_reset(fixture, 2026, 11, 26);
        nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
        if (i == 0)
        {
            fixture->input.collection_complete = false;
        }
        else if (i == 1)
        {
            fixture->input.ledger_published = fixture->input.now - 36 * 3600 - 1;
        }
        else if (i == 2)
        {
            fixture->input.collected_end = fixture->input.now - 15 * 60 - 1;
            fixture->input.collected_start = fixture->input.collected_end - 60;
        }
        else if (i == 3)
        {
            fixture->attempts[0].facts_valid = false;
        }
        else if (i == 4)
        {
            fixture->attempts[0].jobs_complete = false;
        }
        else if (i == 5)
        {
            fixture->attempts[0].record_state = NIGHTLY_OBSERVER_RECORD_UNAVAILABLE;
            fixture->attempts[0].record = NULL;
            fixture->attempts[0].published = 0;
        }
        else if (i == 6)
        {
            fixture->attempts[0].record_state = NIGHTLY_OBSERVER_RECORD_EXPIRED;
            fixture->attempts[0].record = NULL;
        }
        else
        {
            fixture->attempts[0].worker_start = 0;
            fixture->attempts[0].worker_end = 0;
        }
        report = nightly_observer_test_report(&fixture->input, &result);
        nightly_expect(test, result == 2, "observer: unavailable, stale, expired or incomplete evidence never produces a successful report");
        free(report);
    }

    for (uint32_t i = 0; i < 5; i += 1)
    {
        nightly_observer_test_reset(fixture, 2026, 11, 26);
        nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
        nightly_observer_test_attempt(fixture, 1, 0, 2026, 11, 26, 1001, 2);
        fixture->rows[0].outcome = NIGHTLY_OUTCOME_BLOCKED_NATIVE;
        fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_FAILURE;
        fixture->attempts[0].hardware_conclusion = NIGHTLY_CONCLUSION_FAILURE;
        nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_NATIVE], BUSTER_NIGHTLY_VALUE, "started");
        nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_BENCHMARK_RESULT], BUSTER_NIGHTLY_VALUE, "failure");
        nightly_copy(fixture->records[0].values[NIGHTLY_FIELD_OUTCOME], BUSTER_NIGHTLY_VALUE, "blocked-native");
        if (i == 0)
        {
            fixture->attempts[0].facts_valid = false;
        }
        else if (i == 1)
        {
            fixture->attempts[0].jobs_complete = false;
        }
        else
        {
            fixture->attempts[0].record_state = i == 2 ? NIGHTLY_OBSERVER_RECORD_UNAVAILABLE : i == 3 ? NIGHTLY_OBSERVER_RECORD_MISSING : NIGHTLY_OBSERVER_RECORD_EXPIRED;
            fixture->attempts[0].record = NULL;
            fixture->attempts[0].published = 0;
        }
        report = nightly_observer_test_report(&fixture->input, &result);
        nightly_expect(test, result == (i < 3 ? 2 : 0), "observer: an earlier failed API read prevents passing; known absent or expired old records cannot erase a proven later full attempt");
        const NightlyObserverCycle cycle = nightly_observe_cycle(&fixture->input, fixture->since);
        nightly_expect(test, cycle.failures == 1, "observer: original ledger failures remain visible after later proof and earlier artifact loss");
        free(report);
    }

    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_attempt(fixture, 0, 0, 2026, 11, 26, 1001, 1);
    fixture->attempts[0].worker_start = 4;
    fixture->attempts[0].worker_end = 7;
    fixture->attempts[0].hardware_finished = nightly_test_madrid(2026, 11, 26, 7, 0);
    fixture->attempts[0].published = nightly_test_madrid(2026, 11, 26, 7, 5);
    fixture->attempts[0].run.updated = fixture->attempts[0].published;
    nightly_observer_test_status(test, fixture, "complete-at-pin", "observer: immutable historical workers can complete by their own narrower deadline");
    fixture->attempts[0].hardware_finished += 1;
    const NightlyObserverCycle historical_late = nightly_observe_cycle(&fixture->input, fixture->since);
    nightly_expect(test, strncmp(historical_late.status, "complete-", 9) != 0, "observer: a historical 07:00 deadline is not silently widened to 08:00");
    fixture->attempts[0].run.conclusion = NIGHTLY_CONCLUSION_FAILURE;
    fixture->attempts[0].hardware_conclusion = NIGHTLY_CONCLUSION_FAILURE;
    fixture->attempts[0].record = NULL;
    fixture->attempts[0].record_state = NIGHTLY_OBSERVER_RECORD_MISSING;
    fixture->attempts[0].published = 0;
    const NightlyObserverCycle historical_failure = nightly_observe_cycle(&fixture->input, fixture->since);
    nightly_expect(test, strcmp(historical_failure.status, "failed") == 0 && historical_failure.deadline == nightly_test_madrid(2026, 11, 26, 7, 0) && historical_failure.report_deadline == nightly_test_madrid(2026, 11, 26, 10, 0), "observer: even failed historical workers retain their narrower hardware deadline and separate reporting grace");

    // Frozen UTC expectations exercise both Madrid transitions without using the implementation as oracle.
    static const struct
    {
        int month;
        int day;
        int64_t start;
        int64_t deadline;
        int64_t hours;
    } transitions[] = {
        { 3, 29, 1774746000, 1774764000, 5 },
        { 10, 25, 1792886400, 1792911600, 7 },
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(transitions); i += 1)
    {
        nightly_observer_test_reset(fixture, 2026, transitions[i].month, transitions[i].day);
        nightly_observer_test_attempt(fixture, 0, 0, 2026, transitions[i].month, transitions[i].day, 1001, 1);
        const NightlyObserverCycle cycle = nightly_observe_cycle(&fixture->input, fixture->since);
        nightly_expect(test, cycle.window.start == transitions[i].start && cycle.deadline == transitions[i].deadline && cycle.deadline - cycle.window.start == transitions[i].hours * 3600, "observer: Madrid's spring gap and autumn repeated hour have the correct hardware bounds");
        nightly_expect(test, cycle.report_deadline == transitions[i].deadline + 2 * 3600 && strcmp(cycle.status, "complete-at-pin") == 0, "observer: both DST transition cycles retain separate reporting grace and valid native completion");
    }

    nightly_observer_test_reset(fixture, 2026, 2, 28);
    nightly_copy(fixture->since, sizeof(fixture->since), "2026-02-30");
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 2, "observer: impossible calendar dates fail closed instead of normalizing silently");
    free(report);
    nightly_observer_test_reset(fixture, 2026, 11, 26);
    nightly_observer_test_fresh(fixture, nightly_test_madrid(2030, 11, 26, 11, 0));
    report = nightly_observer_test_report(&fixture->input, &result);
    nightly_expect(test, result == 2, "observer: exceeding the bounded cohort never silently drops old missing cycles");
    free(report);
    free(fixture);
}

