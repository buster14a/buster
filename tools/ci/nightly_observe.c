// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by nightly.c. The independent calendar observer (#3627) reads the coordinator's existing ledger and
// immutable coverage records; it never repairs them, admits work or creates a second completion ledger. The thin
// nightly-observe.sh collector supplies bounded, per-attempt API facts and receipts. Calendar enumeration is here,
// not inferred from whichever pins, workflow runs or reports happened to survive collection.

#define BUSTER_NIGHTLY_OBSERVER_SINCE "2026-11-07"
#define BUSTER_NIGHTLY_OBSERVER_GRACE (2 * 3600)
#define BUSTER_NIGHTLY_OBSERVER_FRESHNESS (36 * 3600)
#define BUSTER_NIGHTLY_OBSERVER_COLLECTION (15 * 60)

typedef enum NightlyObserverRecordState NightlyObserverRecordState;
enum NightlyObserverRecordState
{
    NIGHTLY_OBSERVER_RECORD_PRESENT,
    NIGHTLY_OBSERVER_RECORD_MISSING,
    NIGHTLY_OBSERVER_RECORD_EXPIRED,
    NIGHTLY_OBSERVER_RECORD_UNAVAILABLE,
};
static const char* const nightly_observer_record_names[] = { "present", "missing", "expired", "unavailable" };

typedef struct NightlyObserverAttempt NightlyObserverAttempt;
struct NightlyObserverAttempt
{
    NightlyRun run;
    int64_t started;
    uint64_t job;
    int64_t hardware_started;
    int64_t hardware_finished;
    bool facts_valid;
    bool jobs_complete;
    NightlyConclusion hardware_conclusion;
    bool hardware_completed;
    bool hardware_present;
    NightlyRecord* record;
    NightlyObserverRecordState record_state;
    int64_t published;
    uint32_t worker_start;
    uint32_t worker_end;
};

typedef struct NightlyObserverInput NightlyObserverInput;
struct NightlyObserverInput
{
    const NightlyLedger* ledger;
    const NightlyObserverAttempt* attempts;
    uint32_t attempt_count;
    const char* since;
    int64_t now;
    int64_t collected_start;
    int64_t collected_end;
    int64_t ledger_fetched;
    int64_t ledger_published;
    bool collection_complete;
    bool ledger_present;
};

typedef struct NightlyObserverCycle NightlyObserverCycle;
struct NightlyObserverCycle
{
    NightlyWindow window;
    const NightlyCycle* pin;
    const NightlyObserverAttempt* latest;
    const char* status;
    const char* reason;
    bool unavailable;
    bool within_grace;
    uint32_t failures;
    uint32_t interruptions;
    int64_t deadline;
    int64_t report_deadline;
};

// Round-trip the civil date: nightly_date alone deliberately permits day overflow for planners.
static bool nightly_observer_date(const char* date, int64_t* midnight)
{
    int year = 0;
    int month = 0;
    int day = 0;
    NightlyLocal check = { 0 };
    bool valid = date != NULL && nightly_date(date) && sscanf(date, "%d-%d-%d", &year, &month, &day) == 3;
    valid = valid && nightly_madrid_instant(year, month, day, 0, midnight) && nightly_madrid(*midnight, &check);
    valid = valid && check.year == year && check.month == month && check.day == day;
    return valid;
}

static bool nightly_observer_sources(const NightlyObserverInput* input)
{
    bool valid = input->collection_complete && input->ledger_present && input->ledger != NULL &&
                 input->collected_start > 0 && input->collected_end >= input->collected_start && input->now >= input->collected_end &&
                 input->now - input->collected_start <= BUSTER_NIGHTLY_OBSERVER_COLLECTION &&
                 input->ledger_fetched >= input->collected_start && input->ledger_fetched <= input->collected_end &&
                 input->ledger_published > 0 && input->ledger_published <= input->ledger_fetched &&
                 input->now - input->ledger_published <= BUSTER_NIGHTLY_OBSERVER_FRESHNESS;
    for (uint32_t i = 0; valid && i < input->attempt_count; i += 1)
    {
        const NightlyObserverAttempt* attempt = &input->attempts[i];
        valid = attempt->facts_valid && attempt->jobs_complete && attempt->record_state != NIGHTLY_OBSERVER_RECORD_UNAVAILABLE;
    }
    return valid;
}

static bool nightly_observer_in_cycle(const NightlyRun* run, const char* date, bool admitted)
{
    bool request_admit = false;
    return run->nightly && run->purpose == NIGHTLY_PURPOSE_SNAPSHOT && run->admit == admitted &&
           nightly_request_this_cycle(run->request, date, &request_admit) && request_admit == admitted;
}

static bool nightly_observer_failed(NightlyOutcome outcome)
{
    return outcome == NIGHTLY_OUTCOME_FAILED || outcome == NIGHTLY_OUTCOME_BLOCKED_HOSTED ||
           outcome == NIGHTLY_OUTCOME_BLOCKED_NATIVE || outcome == NIGHTLY_OUTCOME_REJECTED;
}

// Snapshot workers skip `changes`; their native job defaults its absent MEASURE output to `all`. Coverage records
// retain that absence as `-`, with changes_result=skipped. A targeted output or failed/missing selector is different.
// nightly_test_workflow pins the source side of this contract.
static bool nightly_observer_full_measure(const NightlyRecord* record)
{
    return nightly_is(record, NIGHTLY_FIELD_MEASURE, "all") ||
           (nightly_is(record, NIGHTLY_FIELD_MEASURE, "-") && nightly_is(record, NIGHTLY_FIELD_CHANGES_RESULT, "skipped"));
}

// The declared outcome must agree with its own inputs. Never upgrade declared targeted/plain coverage using the
// purpose field. A mismatch supplies no full-coverage proof.
static bool nightly_observer_record_agrees(const NightlyObserverAttempt* attempt, const NightlyCycle* pin)
{
    const NightlyRun* run = &attempt->run;
    const NightlyRecord* record = attempt->record;
    char id[24];
    char number[12];
    snprintf(id, sizeof(id), "%llu", (unsigned long long)run->id);
    snprintf(number, sizeof(number), "%u", run->attempt);
    const bool valid = record != NULL && nightly_is(record, NIGHTLY_FIELD_RUN, id) &&
                       nightly_is(record, NIGHTLY_FIELD_ATTEMPT, number) && nightly_is(record, NIGHTLY_FIELD_COMMIT, run->head) &&
                       nightly_is(record, NIGHTLY_FIELD_EVENT, "workflow_dispatch") && nightly_is(record, NIGHTLY_FIELD_PURPOSE, "snapshot") &&
                       nightly_is(record, NIGHTLY_FIELD_REQUEST, run->request) && pin != NULL &&
                       nightly_is(record, NIGHTLY_FIELD_SNAPSHOT, pin->snapshot) &&
                       nightly_is(record, NIGHTLY_FIELD_OUTCOME, nightly_outcome_names[nightly_record_outcome(record)]);
    return valid;
}

static NightlyObserverCycle nightly_observe_cycle(const NightlyObserverInput* input, const char* date)
{
    NightlyObserverCycle cycle = {
        .status = "awaiting-evidence",
        .reason = "calendar or evidence unavailable",
        .unavailable = true,
    };
    int64_t midnight = 0;
    const bool date_valid = nightly_observer_date(date, &midnight);
    if (date_valid)
    {
        cycle.window = nightly_window(midnight, 0);
        cycle.deadline = cycle.window.deadline;
        cycle.report_deadline = cycle.deadline + BUSTER_NIGHTLY_OBSERVER_GRACE;
        cycle.within_grace = input->now < cycle.report_deadline;
    }
    const NightlyLedger* ledger = input->ledger;
    uint64_t latest_row_run = 0;
    uint32_t latest_row_attempt = 0;
    for (uint32_t i = 0; ledger != NULL && i < ledger->cycle_count; i += 1)
    {
        if (strcmp(ledger->cycles[i].date, date) == 0)
        {
            cycle.pin = &ledger->cycles[i];
        }
    }
    for (uint32_t i = 0; ledger != NULL && i < ledger->row_count; i += 1)
    {
        const NightlyRow* row = &ledger->rows[i];
        bool admit = false;
        if (row->purpose == NIGHTLY_PURPOSE_SNAPSHOT && nightly_request_this_cycle(row->request, date, &admit) && admit)
        {
            cycle.failures += nightly_observer_failed(row->outcome);
            cycle.interruptions += row->outcome == NIGHTLY_OUTCOME_INTERRUPTED;
            if (nightly_later(row->run, row->attempt, latest_row_run, latest_row_attempt))
            {
                latest_row_run = row->run;
                latest_row_attempt = row->attempt;
            }
        }
    }
    for (uint32_t i = 0; i < input->attempt_count; i += 1)
    {
        const NightlyObserverAttempt* attempt = &input->attempts[i];
        const NightlyRun* run = &attempt->run;
        if (nightly_observer_in_cycle(run, date, true))
        {
            if (cycle.latest == NULL || nightly_later(run->id, run->attempt, cycle.latest->run.id, cycle.latest->run.attempt))
            {
                cycle.latest = attempt;
            }
            // Preserve the original harvested failure, even when a late artifact supplies stronger evidence. Count
            // an unharvested attempt exactly once, so repeated observation does not manufacture extra failures.
            if (ledger == NULL || !nightly_has_row(ledger, run->id, run->attempt))
            {
                const NightlyOutcome outcome = attempt->record != NULL ? nightly_record_outcome(attempt->record) : NIGHTLY_OUTCOME_INVALID;
                cycle.failures += nightly_observer_failed(outcome) || run->conclusion == NIGHTLY_CONCLUSION_FAILURE;
                cycle.interruptions += outcome == NIGHTLY_OUTCOME_INTERRUPTED || run->conclusion == NIGHTLY_CONCLUSION_CANCELLED || run->conclusion == NIGHTLY_CONCLUSION_TIMED_OUT;
            }
        }
    }
    const NightlyObserverAttempt* attempt = cycle.latest;
    const NightlyRun* run = attempt != NULL ? &attempt->run : NULL;
    const NightlyRecord* record = attempt != NULL ? attempt->record : NULL;
    int64_t worker_start = 0;
    int64_t worker_end = 0;
    const NightlyLocal local = cycle.window.local;
    const bool worker = attempt != NULL && cycle.window.valid && nightly_worker_hours(attempt->worker_start, attempt->worker_end) &&
                        nightly_madrid_first(local.year, local.month, local.day, (int)attempt->worker_start, &worker_start) &&
                        nightly_madrid_instant(local.year, local.month, local.day, (int)attempt->worker_end, &worker_end);
    if (worker && worker_end < cycle.deadline)
    {
        cycle.deadline = worker_end;
    }
    char branch[64] = { 0 };
    if (run != NULL)
    {
        snprintf(branch, sizeof(branch), "nightly/%s", run->head);
    }
    const bool missing_attempt = latest_row_run != 0 && (run == NULL || nightly_later(latest_row_run, latest_row_attempt, run->id, run->attempt));
    if (!date_valid || !cycle.window.valid)
    {
        cycle.reason = "calendar unavailable";
    }
    else if (!nightly_observer_sources(input))
    {
        cycle.reason = "evidence-unavailable: incomplete collection, unavailable ledger or stale receipt";
    }
    else if (missing_attempt)
    {
        cycle.reason = "evidence-unavailable: latest ledger attempt has no fresh API evidence";
    }
    else if (attempt == NULL)
    {
        cycle.status = "not-attempted";
        cycle.reason = cycle.pin == NULL ? "no pin and no snapshot admission" : "pin exists, no snapshot admission";
        cycle.unavailable = false;
    }
    else if (!attempt->facts_valid || !attempt->jobs_complete || !run->workflow || run->repository != BUSTER_NIGHTLY_REPOSITORY ||
             run->event != NIGHTLY_EVENT_DISPATCH || strcmp(run->head, run->target) != 0 || strcmp(run->branch, branch) != 0)
    {
        cycle.reason = "evidence-unavailable: incomplete or mismatched run/attempt facts";
    }
    else if (record != NULL && !nightly_observer_record_agrees(attempt, cycle.pin))
    {
        cycle.reason = "evidence-unavailable: coverage identity, pin or outcome disagrees";
    }
    else if (record != NULL && nightly_is(record, NIGHTLY_FIELD_OUTCOME, "deferred"))
    {
        cycle.status = "not-attempted";
        cycle.reason = "snapshot admission deferred or refused by its hardware window";
        cycle.unavailable = false;
    }
    else if (attempt->hardware_conclusion == NIGHTLY_CONCLUSION_CANCELLED || attempt->hardware_conclusion == NIGHTLY_CONCLUSION_TIMED_OUT ||
             run->conclusion == NIGHTLY_CONCLUSION_CANCELLED || run->conclusion == NIGHTLY_CONCLUSION_TIMED_OUT)
    {
        cycle.status = "interrupted";
        cycle.reason = "snapshot attempt cancelled or timed out";
        cycle.unavailable = false;
    }
    else if (attempt->hardware_conclusion == NIGHTLY_CONCLUSION_FAILURE || (record != NULL && nightly_observer_failed(nightly_record_outcome(record))))
    {
        cycle.status = "failed";
        cycle.reason = record != NULL ? nightly_value(record, NIGHTLY_FIELD_OUTCOME) : "native job failed; coverage record unavailable";
        cycle.unavailable = false;
    }
    else if (run->conclusion == NIGHTLY_CONCLUSION_FAILURE && (record == NULL || !nightly_is(record, NIGHTLY_FIELD_OUTCOME, "complete")))
    {
        cycle.status = "failed";
        cycle.reason = "snapshot attempt failed before full native completion";
        cycle.unavailable = false;
    }
    else if (attempt->record_state != NIGHTLY_OBSERVER_RECORD_PRESENT || record == NULL)
    {
        cycle.reason = "evidence-unavailable: coverage record missing, expired, malformed or unreadable";
    }
    else if (!run->completed)
    {
        cycle.reason = "snapshot attempt still in progress";
    }
    else if (cycle.pin == NULL || cycle.pin->pinned < cycle.window.pin || cycle.pin->pinned >= cycle.window.deadline || cycle.pin->pinned > run->created)
    {
        cycle.reason = "evidence-unavailable: original calendar pin missing or outside its pin window";
    }
    else if (!worker)
    {
        cycle.reason = "evidence-unavailable: immutable worker window unknown";
    }
    else if (!nightly_is(record, NIGHTLY_FIELD_COVERAGE, "full") || !nightly_observer_full_measure(record) ||
             !nightly_is(record, NIGHTLY_FIELD_NATIVE_EVIDENCE, "run") || !nightly_is(record, NIGHTLY_FIELD_NATIVE, "passed") ||
             !nightly_is(record, NIGHTLY_FIELD_MEASUREMENT_EVIDENCE, "run") || !nightly_is(record, NIGHTLY_FIELD_HARDWARE, "admit") ||
             !nightly_is(record, NIGHTLY_FIELD_WINDOW, "open") || !nightly_is(record, NIGHTLY_FIELD_BENCHMARK_RESULT, "success") ||
             !nightly_is(record, NIGHTLY_FIELD_OUTCOME, "complete"))
    {
        cycle.reason = "evidence-unavailable: record does not prove the full native inventory and measurement";
    }
    else if (!attempt->hardware_present || !attempt->hardware_completed || attempt->hardware_conclusion != NIGHTLY_CONCLUSION_SUCCESS ||
             attempt->job == 0 || attempt->started < run->created || attempt->started <= 0 || attempt->started > attempt->hardware_started ||
             attempt->hardware_started <= 0 || attempt->hardware_finished < attempt->hardware_started ||
             attempt->published < attempt->hardware_finished || attempt->published > input->collected_end ||
             run->updated < run->created || run->updated > input->collected_end)
    {
        cycle.reason = "evidence-unavailable: native job timing or attempt ownership unproven";
    }
    else if (attempt->hardware_started < worker_start || attempt->hardware_finished > cycle.deadline)
    {
        cycle.status = "failed";
        cycle.reason = "hardware outside the original window; later recovery cannot repair its deadline";
        cycle.unavailable = false;
    }
    else
    {
        cycle.status = strcmp(run->head, cycle.pin->snapshot) == 0 ? "complete-at-pin" : "complete-substitute";
        cycle.reason = attempt->published > cycle.report_deadline ? "on-time hardware; late coverage publication" : "on-time full native measurement";
        cycle.unavailable = false;
    }
    return cycle;
}

static void nightly_observer_time(FILE* output, int64_t instant)
{
    const time_t seconds = (time_t)instant;
    struct tm parts;
    char text[32] = { 0 };
    if (instant > 0 && gmtime_r(&seconds, &parts) != NULL && strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &parts) != 0)
    {
        fputs(text, output);
    }
    else
    {
        fputs("unavailable", output);
    }
}

// Operator prose is validated record data but can contain Markdown punctuation. Display it literally.
static void nightly_observer_text(FILE* output, const char* text)
{
    for (const char* at = text; *at != 0; at += 1)
    {
        if (strchr("\\`*_{}[]<>()|", *at) != NULL)
        {
            fputc('\\', output);
        }
        fputc(*at, output);
    }
}

static void nightly_observer_attempt(FILE* output, const NightlyObserverAttempt* attempt, bool preparation)
{
    const NightlyRun* run = &attempt->run;
    const NightlyRecord* record = attempt->record;
    fprintf(output, "\n- %s [run %llu, attempt %u](https://github.com/buster14a/buster/actions/runs/%llu/attempts/%u): SHA `%s`, request `%s`; API `%s`.",
            preparation ? "Hosted preparation" : "Snapshot admission", (unsigned long long)run->id, run->attempt,
            (unsigned long long)run->id, run->attempt, run->head, run->request, nightly_conclusion_names[run->conclusion]);
    if (record != NULL)
    {
        fprintf(output, " Declared coverage `%s`, native `%s` (`%s`), hardware `%s`, record outcome `%s`; inventory `%s`, worker `%s`.",
                nightly_value(record, NIGHTLY_FIELD_COVERAGE), nightly_value(record, NIGHTLY_FIELD_NATIVE), nightly_value(record, NIGHTLY_FIELD_NATIVE_EVIDENCE),
                nightly_value(record, NIGHTLY_FIELD_HARDWARE), nightly_value(record, NIGHTLY_FIELD_OUTCOME), nightly_value(record, NIGHTLY_FIELD_INVENTORY), nightly_value(record, NIGHTLY_FIELD_WORKFLOW));
        fprintf(output, " Declared measure `%s`, selector `%s`, host `%s`.", nightly_value(record, NIGHTLY_FIELD_MEASURE),
                nightly_value(record, NIGHTLY_FIELD_CHANGES_RESULT), nightly_value(record, NIGHTLY_FIELD_HOST));
        fprintf(output, " Hosted diagnostics: scope `%s`, tests `%s`, fuzz `%s`.", nightly_value(record, NIGHTLY_FIELD_SCOPE),
                nightly_value(record, NIGHTLY_FIELD_TEST_RESULT), nightly_value(record, NIGHTLY_FIELD_FUZZ_RESULT));
        fputs(" Operator override: ", output);
        nightly_observer_text(output, nightly_value(record, NIGHTLY_FIELD_OVERRIDE));
        fputs(".", output);
    }
    else
    {
        fprintf(output, " Coverage record `%s`; native qualification unconfirmed.", nightly_observer_record_names[attempt->record_state]);
    }
    if (!preparation)
    {
        if (attempt->hardware_present)
        {
            fprintf(output, " Native [job %llu](https://github.com/buster14a/buster/actions/runs/%llu/job/%llu): `%s`; started ",
                    (unsigned long long)attempt->job, (unsigned long long)run->id, (unsigned long long)attempt->job, nightly_conclusion_names[attempt->hardware_conclusion]);
        }
        else
        {
            fputs(" Native job not observed; started ", output);
        }
        nightly_observer_time(output, attempt->hardware_started);
        fputs("; finished ", output);
        nightly_observer_time(output, attempt->hardware_finished);
        fputs("; coverage published ", output);
        nightly_observer_time(output, attempt->published);
        fputs(".", output);
    }
    fputc('\n', output);
}

static int nightly_observe_report(const NightlyObserverInput* input, FILE* output)
{
    int64_t day = 0;
    const char* since = input->since != NULL ? input->since : "";
    bool valid = nightly_observer_date(since, &day);
    fprintf(output, "# Independent nightly calendar observation\n\nExpected first cycle: `%s`%s. Observed at ", since,
            strcmp(since, BUSTER_NIGHTLY_OBSERVER_SINCE) != 0 ? " (explicit diagnostic range; not the scheduled cohort)" : "");
    nightly_observer_time(output, input->now);
    fputs(".\n\nHardware must finish by **08:00 Europe/Madrid** (or the immutable worker's earlier deadline). "
          "Coverage publication has a separate **two-hour grace, until 10:00 Madrid**. A late publication may prove on-time hardware; "
          "later hardware cannot repair a closed cycle. The latest admitted snapshot attempt determines its status; "
          "targeted work and hosted preparation never complete a full measurement.\n\nCollection: ", output);
    nightly_observer_time(output, input->collected_start);
    fputs(" to ", output);
    nightly_observer_time(output, input->collected_end);
    fputs("; ledger published ", output);
    nightly_observer_time(output, input->ledger_published);
    fprintf(output, ". Evidence receipt: **%s**.\n\n", nightly_observer_sources(input) ? "available" : "evidence-unavailable");
    fputs("This is a bounded observation, not an atomic API snapshot. Receipt freshness ends at ", output);
    nightly_observer_time(output, input->collected_start + BUSTER_NIGHTLY_OBSERVER_COLLECTION);
    fputs("; an older saved green report is historical evidence, not a current pass.\n\n", output);
    uint32_t count = 0;
    uint32_t complete = 0;
    uint32_t missed = 0;
    uint32_t unavailable = 0;
    bool finished = false;
    while (valid && !finished && count < BUSTER_NIGHTLY_CYCLES)
    {
        NightlyLocal local;
        valid = nightly_madrid(day, &local);
        char date[11];
        if (valid)
        {
            snprintf(date, sizeof(date), "%04d-%02d-%02d", local.year, local.month, local.day);
            const NightlyObserverCycle cycle = nightly_observe_cycle(input, date);
            finished = cycle.window.valid && input->now < cycle.window.deadline;
            valid = cycle.window.valid;
            if (valid && !finished)
            {
                count += 1;
                const bool done = strcmp(cycle.status, "complete-at-pin") == 0 || strcmp(cycle.status, "complete-substitute") == 0;
                complete += done;
                unavailable += cycle.unavailable;
                missed += !done && !cycle.unavailable;
                fprintf(output, "## %s — %s\n\n%s. Reporting grace: %s. Hardware deadline: ", date, cycle.status, cycle.reason, cycle.within_grace ? "open" : "elapsed");
                nightly_observer_time(output, cycle.deadline);
                fputs("; reporting deadline: ", output);
                nightly_observer_time(output, cycle.report_deadline);
                fputs(".\n\n", output);
                if (cycle.pin != NULL)
                {
                    fprintf(output, "Original pin: `%s`; [coordinator %llu](https://github.com/buster14a/buster/actions/runs/%llu).\n\n",
                            cycle.pin->snapshot, (unsigned long long)cycle.pin->coordinator, (unsigned long long)cycle.pin->coordinator);
                }
                else
                {
                    fputs("Original pin: unavailable (the date is still an expected cycle).\n\n", output);
                }
                fprintf(output, "Retained admission history: %u failed/blocked, %u interrupted.\n", cycle.failures, cycle.interruptions);
                for (uint32_t i = 0; i < input->attempt_count; i += 1)
                {
                    const NightlyObserverAttempt* attempt = &input->attempts[i];
                    const bool preparation = nightly_observer_in_cycle(&attempt->run, date, false);
                    if (preparation || nightly_observer_in_cycle(&attempt->run, date, true))
                    {
                        nightly_observer_attempt(output, attempt, preparation);
                    }
                }
                const NightlyLedger* ledger = input->ledger;
                for (uint32_t i = 0; ledger != NULL && i < ledger->row_count; i += 1)
                {
                    const NightlyRow* row = &ledger->rows[i];
                    bool admit = false;
                    if (row->purpose == NIGHTLY_PURPOSE_SNAPSHOT && nightly_request_this_cycle(row->request, date, &admit))
                    {
                        fprintf(output, "\n- Immutable ledger: [run %llu, attempt %u](https://github.com/buster14a/buster/actions/runs/%llu/attempts/%u), SHA `%s`, `%s`, coverage `%s`, %s.\n",
                                (unsigned long long)row->run, row->attempt, (unsigned long long)row->run, row->attempt, row->commit,
                                nightly_outcome_names[row->outcome], nightly_coverage_names[row->coverage], admit ? "admitted snapshot" : "hosted preparation");
                    }
                }
                fputc('\n', output);
                valid = nightly_madrid_instant(local.year, local.month, local.day + 1, 0, &day);
            }
        }
    }
    if (valid && !finished && count == BUSTER_NIGHTLY_CYCLES)
    {
        const NightlyWindow next = nightly_window(day, 0);
        valid = next.valid && input->now < next.deadline;
    }
    const bool unknown = !valid || !nightly_observer_sources(input) || unavailable != 0 || count == 0;
    const int status = unknown ? 2 : missed != 0 ? 1 : 0;
    fprintf(output, "## Observation result\n\n%u closed cycles: %u complete, %u known incomplete, %u awaiting evidence. **%s**.\n",
            count, complete, missed, unavailable, status == 0 ? "complete" : status == 1 ? "incomplete" : "evidence-unavailable");
    if (!valid)
    {
        fputs("\nThe calendar input is invalid or exceeds the 1,024-cycle bound; no omitted cycle is credited.\n", output);
    }
    fputs("\nThis observation does not change the ledger, retry work, waive failures or certify hosted preparation. "
          "Retain this receipt and its inputs when documenting a recovery.\n", output);
    return ferror(output) ? 2 : status;
}

// All errors leave an empty text and return false, allowing the caller to enumerate the calendar despite a failed
// input. A failed or truncated read is never turned into an apparently empty, passing collection.
static bool nightly_observer_read(const char* path, NightlyText* text)
{
    FILE* file = fopen(path, "rb");
    bool valid = file != NULL;
    long size = 0;
    if (valid)
    {
        valid = fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) >= 0 && size < (long)BUSTER_NIGHTLY_FILE && fseek(file, 0, SEEK_SET) == 0;
    }
    if (valid)
    {
        text->data = nightly_allocate((size_t)size + 1, 1);
        text->length = (size_t)size;
        valid = fread(text->data, 1, text->length, file) == text->length && !ferror(file);
    }
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    if (!valid)
    {
        free(text->data);
        *text = (NightlyText){ 0 };
    }
    return valid;
}

// Exactly one collection receipt and ledger receipt, plus one row per run attempt. The per-attempt files use the
// existing strict facts and coverage formats; neither protocol is duplicated and nothing is rewritten.
static bool nightly_observer_manifest(NightlyText text, NightlyObserverInput* input, NightlyObserverAttempt* attempts)
{
    size_t offset = 0;
    char line[BUSTER_NIGHTLY_LINE];
    bool valid = true;
    bool collection = false;
    bool ledger = false;
    while (valid && nightly_next_line(text, &offset, line, &valid))
    {
        char* fields[BUSTER_NIGHTLY_FIELDS];
        const size_t count = valid ? nightly_split(line, fields, BUSTER_NIGHTLY_FIELDS) : 0;
        if (valid && count == 4 && strcmp(fields[0], "collection") == 0 && !collection)
        {
            valid = nightly_epoch(fields[1], &input->collected_start) && nightly_epoch(fields[2], &input->collected_end) &&
                    (strcmp(fields[3], "complete") == 0 || strcmp(fields[3], "unavailable") == 0);
            input->collection_complete = valid && strcmp(fields[3], "complete") == 0;
            collection = true;
        }
        else if (valid && count == 4 && strcmp(fields[0], "ledger") == 0 && !ledger)
        {
            uint64_t published = 0;
            valid = nightly_epoch(fields[1], &input->ledger_fetched) && nightly_number(fields[3], INT64_MAX, &published) &&
                    (strcmp(fields[2], "present") == 0 || strcmp(fields[2], "missing") == 0 || strcmp(fields[2], "unavailable") == 0);
            input->ledger_published = (int64_t)published;
            input->ledger_present = valid && strcmp(fields[2], "present") == 0;
            ledger = true;
        }
        else if (valid && count == 12 && strcmp(fields[0], "attempt") == 0 && input->attempt_count < BUSTER_NIGHTLY_RUNS)
        {
            NightlyObserverAttempt attempt = { 0 };
            uint64_t values[8] = { 0 };
            const uint32_t columns[] = { 2, 3, 4, 5, 6, 9, 10, 11 };
            valid = nightly_id(fields[1], &attempt.run.id);
            for (uint32_t i = 0; valid && i < BUSTER_NIGHTLY_COUNT(columns); i += 1)
            {
                valid = nightly_number(fields[columns[i]], INT64_MAX, &values[i]);
            }
            const size_t record = nightly_find(fields[8], nightly_observer_record_names, BUSTER_NIGHTLY_COUNT(nightly_observer_record_names));
            valid = valid && values[0] > 0 && values[0] <= 1000 && values[6] <= 23 && values[7] <= 23 &&
                    record < BUSTER_NIGHTLY_COUNT(nightly_observer_record_names) &&
                    (strcmp(fields[7], "complete") == 0 || strcmp(fields[7], "unavailable") == 0);
            attempt.run.attempt = (uint32_t)values[0];
            for (uint32_t i = 0; valid && i < input->attempt_count; i += 1)
            {
                valid = attempts[i].run.id != attempt.run.id || attempts[i].run.attempt != attempt.run.attempt;
            }
            if (valid)
            {
                attempt.started = (int64_t)values[1];
                attempt.job = values[2];
                attempt.hardware_started = (int64_t)values[3];
                attempt.hardware_finished = (int64_t)values[4];
                attempt.jobs_complete = strcmp(fields[7], "complete") == 0;
                attempt.record_state = (NightlyObserverRecordState)record;
                attempt.published = (int64_t)values[5];
                attempt.worker_start = (uint32_t)values[6];
                attempt.worker_end = (uint32_t)values[7];
                attempt.hardware_conclusion = NIGHTLY_CONCLUSION_NONE;
                attempts[input->attempt_count] = attempt;
                input->attempt_count += 1;
            }
        }
        else
        {
            valid = false;
        }
    }
    return valid && collection && ledger;
}

static void nightly_observer_load_attempt(NightlyObserverAttempt* attempt, const char* directory, const char* records, NightlyFacts* facts)
{
    char path[4096];
    const uint64_t run = attempt->run.id;
    const uint32_t number = attempt->run.attempt;
    int length = snprintf(path, sizeof(path), "%s/%llu-%u.tsv", directory, (unsigned long long)run, number);
    NightlyText text = { 0 };
    facts->run_count = 0;
    facts->job_count = 0;
    facts->pull_count = 0;
    attempt->facts_valid = length > 0 && (size_t)length < sizeof(path) && nightly_observer_read(path, &text) && nightly_facts_parse(facts, text) &&
                           facts->run_count == 1 && facts->pull_count == 0 && facts->runs[0].id == run && facts->runs[0].attempt == number;
    free(text.data);
    if (attempt->facts_valid)
    {
        attempt->run = facts->runs[0];
        const NightlyRun* fact = &attempt->run;
        char branch[64];
        char date[11];
        bool admitted = false;
        int64_t midnight = 0;
        snprintf(branch, sizeof(branch), "nightly/%s", fact->head);
        attempt->facts_valid = fact->workflow && fact->repository == BUSTER_NIGHTLY_REPOSITORY && fact->event == NIGHTLY_EVENT_DISPATCH &&
                               fact->nightly && fact->purpose == NIGHTLY_PURPOSE_SNAPSHOT && strcmp(fact->head, fact->target) == 0 &&
                               strcmp(fact->branch, branch) == 0 && nightly_request(fact->request, date, &admitted) && admitted == fact->admit &&
                               nightly_observer_date(date, &midnight);
        for (uint32_t i = 0; i < facts->job_count; i += 1)
        {
            attempt->facts_valid = attempt->facts_valid && facts->jobs[i].run == run;
        }
        const NightlyJob* hardware = nightly_find_job(facts, run, BUSTER_NIGHTLY_HARDWARE_JOB);
        attempt->hardware_present = hardware != NULL;
        if (hardware != NULL)
        {
            attempt->hardware_conclusion = hardware->conclusion;
            attempt->hardware_completed = hardware->completed;
        }
        attempt->facts_valid = attempt->facts_valid && (hardware != NULL) == (attempt->job != 0);
    }
    if (attempt->record_state == NIGHTLY_OBSERVER_RECORD_PRESENT)
    {
        length = snprintf(path, sizeof(path), "%s/%llu-%u.tsv", records, (unsigned long long)run, number);
        text = (NightlyText){ 0 };
        NightlyRecord* record = nightly_allocate(1, sizeof(NightlyRecord));
        const bool valid = length > 0 && (size_t)length < sizeof(path) && nightly_observer_read(path, &text) && nightly_record_parse(text, record);
        if (valid)
        {
            attempt->record = record;
        }
        else
        {
            free(record);
            attempt->record_state = NIGHTLY_OBSERVER_RECORD_UNAVAILABLE;
        }
        free(text.data);
    }
}
