// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by timings.c. The collector emits bounded, tab-separated facts in
// run/attempt -> job -> step order. This reader owns classification, interval
// accounting and statistics; JSON, credentials and network access stay outside.
//
// accounting_job_finish partitions a job into disjoint intervals. Conflicting
// step intervals are an explicit overlap bucket, and gaps are unclassified.
// Lock hold is an overlay, never another additive phase. Administrative report
// generation is outside the compiler and benchmark hot paths.

#define BUSTER_ACCOUNTING_RUNS 4096
#define BUSTER_ACCOUNTING_JOBS 65536
#define BUSTER_ACCOUNTING_STEPS 4096
#define BUSTER_ACCOUNTING_NOTES 512
#define BUSTER_ACCOUNTING_GROUPS 512
#define BUSTER_ACCOUNTING_LINE 4096
#define BUSTER_ACCOUNTING_TEXT 512
#define BUSTER_ACCOUNTING_TIME_MAX UINT64_C(253402300799)
#define BUSTER_ACCOUNTING_UNKNOWN UINT64_MAX

typedef enum AccountingPhase
{
    ACCOUNTING_SETUP,
    ACCOUNTING_CORRECTNESS,
    ACCOUNTING_MEASUREMENT,
    ACCOUNTING_UPLOAD,
    ACCOUNTING_OVERHEAD,
    ACCOUNTING_LOCK_WAIT,
    ACCOUNTING_UNCLASSIFIED,
    ACCOUNTING_OVERLAP,
    ACCOUNTING_PHASE_COUNT,
} AccountingPhase;

static const char* const accounting_events[] = {
    "pr",
    "queue",
    "main",
    "targeted",
    "full",
    "preparation",
    "reporting",
    "other",
};
static const char* const accounting_cohorts[] = {
    "all",
    "before",
    "after",
};
static const char* const accounting_classes[] = {
    "hosted",
    "self-hosted",
    "none",
    "unknown",
};
static const char* const accounting_fates[] = {
    "na",
    "merged",
    "live",
    "invalidated",
    "unknown",
    "superseded-possible",
};
static const char* const accounting_outcomes[] = {
    "success",
    "failure",
    "cancelled",
    "timed_out",
    "skipped",
    "neutral",
    "action_required",
    "stale",
    "startup_failure",
    "unknown",
    "partial",
};

typedef struct AccountingRun AccountingRun;
struct AccountingRun
{
    uint64_t id;
    uint32_t attempt;
    size_t event;
    size_t cohort;
    size_t fate;
    size_t outcome;
    size_t group;
    size_t logical;
    size_t first_job;
    size_t jobs;
    uint64_t created;
    uint64_t started;
    uint64_t updated;
    bool terminal;
    bool jobs_complete;
    char workflow[128];
    char sha[41];
    char branch[BUSTER_ACCOUNTING_TEXT];
};

typedef struct AccountingJob AccountingJob;
struct AccountingJob
{
    uint64_t id;
    size_t run;
    size_t runner_class;
    size_t outcome;
    uint64_t created;
    uint64_t started;
    uint64_t completed;
    uint64_t view_start;
    uint64_t view_end;
    uint64_t execution;
    uint64_t queue;
    uint64_t feedback;
    uint64_t lock_wait;
    uint64_t lock_hold;
    uint64_t phases[ACCOUNTING_PHASE_COUNT];
    size_t missing_steps;
    size_t partial_steps;
    size_t steps;
    bool terminal;
    bool steps_complete;
    bool allocated;
    bool exact;
    bool observed;
    bool lock_seen;
    char name[BUSTER_ACCOUNTING_TEXT];
    char labels[BUSTER_ACCOUNTING_TEXT];
    char cpu[BUSTER_ACCOUNTING_TEXT];
};

typedef struct AccountingStep AccountingStep;
struct AccountingStep
{
    uint64_t number;
    uint64_t start;
    uint64_t end;
    AccountingPhase phase;
    bool partial;
};

typedef struct AccountingEdge AccountingEdge;
struct AccountingEdge
{
    uint64_t time;
    int delta;
    AccountingPhase phase;
};

typedef struct AccountingLogical AccountingLogical;
struct AccountingLogical
{
    uint64_t id;
    size_t latest;
    size_t attempts;
    size_t group;
    size_t jobs;
    size_t unknown_jobs;
    size_t partial_jobs;
    size_t tail;
    size_t longest;
    uint64_t created;
    uint64_t feedback;
    uint64_t completion;
    uint64_t resources[4];
    uint64_t retry;
    uint64_t union_seconds;
    uint64_t queue_max;
    bool complete;
    bool classes_known;
    bool wall_complete;
};

typedef struct AccountingGroup AccountingGroup;
struct AccountingGroup
{
    size_t event;
    size_t cohort;
    char workflow[128];
};

typedef struct AccountingNote AccountingNote;
struct AccountingNote
{
    char source[128];
    char text[BUSTER_ACCOUNTING_TEXT * 2];
};

typedef struct Accounting Accounting;
struct Accounting
{
    AccountingRun* runs;
    AccountingJob* jobs;
    AccountingLogical* logical;
    AccountingGroup* groups;
    AccountingStep* steps;
    AccountingEdge* edges;
    AccountingNote* notes;
    uint64_t* ids;
    uint64_t* values;
    size_t run_count;
    size_t job_count;
    size_t logical_count;
    size_t group_count;
    size_t step_count;
    size_t note_count;
    size_t current_run;
    size_t current_job;
    uint64_t snapshot;
    uint64_t split;
    uint64_t capacity;
    uint64_t lock_start;
    uint64_t lock_acquired;
    uint64_t lock_released;
    bool meta;
    char capacity_source[BUSTER_ACCOUNTING_TEXT];
};

static bool accounting_copy(char* destination, size_t capacity, const char* source)
{
    const size_t length = strlen(source);
    bool valid = length != 0 && length < capacity;
    for (size_t i = 0; i < length && valid; i += 1)
    {
        valid = (unsigned char)source[i] >= 32 && source[i] != 127;
    }
    if (valid)
    {
        memcpy(destination, source, length + 1);
    }
    return valid;
}

static bool accounting_optional(const char* text, uint64_t limit, uint64_t* value)
{
    bool valid = true;
    if (strcmp(text, "-") == 0)
    {
        *value = BUSTER_ACCOUNTING_UNKNOWN;
    }
    else
    {
        valid = timing_number(text, limit, value);
    }
    return valid;
}

static bool accounting_duration(uint64_t start, uint64_t end, uint64_t* duration)
{
    const bool valid = start != BUSTER_ACCOUNTING_UNKNOWN && end != BUSTER_ACCOUNTING_UNKNOWN && end >= start;
    *duration = valid ? end - start : BUSTER_ACCOUNTING_UNKNOWN;
    return valid;
}

static bool accounting_prefix(const char* text, const char* prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool accounting_any_prefix(const char* text, const char* const* prefixes, size_t count)
{
    bool found = false;
    for (size_t i = 0; i < count && !found; i += 1)
    {
        found = accounting_prefix(text, prefixes[i]);
    }
    return found;
}

static AccountingPhase accounting_phase(const char* name)
{
    // Match the actual measurement-producing step families before general
    // checks and setup. A failed or cancelled measurement still consumed time.
    static const char* const measurements[] = {
        "Benchmark ",
        "Measure ",
        "Attribute ",
        "Compare ",
        "Time ",
        "Profile ",
        "Gauge ",
        "Reproduce a sample of the Zen 5 model's rows",
        "Reproduce the Zen 5 sample in two more processes",
    };
    static const char* const uploads[] = {
        "Upload ",
        "Publish ",
        "Preserve ",
        "Keep ",
        "Record the ",
        "Write the coverage",
        "Write the vector",
    };
    static const char* const setup[] = {
        "Check out ",
        "Checkout ",
        "Set up ",
        "Setup ",
        "Build ",
        "Restore ",
        "Install ",
        "Fetch ",
        "Stage ",
        "Download ",
        "Acquire ",
        "Pack ",
        "Name ",
        "Verify benchmark host",
        "Verify and install ",
        "Verify the restored ",
        "Verify the staged ",
        "Wait for the ",
        "Require this row's compiler product",
        "Report host CPU",
        "Report sanitizer host",
        "Record the host and select",
        "Probe this runner",
        "Set the vector item's configuration",
        "Set fatal sanitizer policy",
    };
    static const char* const overhead[] = {
        "Post ",
        "Complete job",
        "Initialize containers",
        "Stop containers",
        "Set up runner",
        "Run actions/checkout@",
        "Run actions/cache@",
        "Take the vector item",
        "Assign the draw",
        "Cancel ",
        "Harvest",
        "Admit ",
        "Seed the ",
        "Seed reusable ",
        "Report the hosted compiler",
        "Remove the comparison build worktree",
    };
    static const char* const correctness[] = {
        "Test ",
        "Check ",
        "Verify ",
        "Require ",
        "Reject ",
        "Confirm ",
        "Run correctness ",
        "Run complete native ",
        "Run SQLite",
        "Run each refusal",
        "Run now()",
        "Compile and run ",
        "Compile main.c alone",
        "Parse the compiler",
        "Round trips,",
        "Address and undefined-behavior",
        "Seeded bounded wire-format",
        "Fuzz ",
        "Run frontend ",
        "Run ELF ",
        "Run crash",
        "Run the ",
    };
    AccountingPhase phase = ACCOUNTING_UNCLASSIFIED;
    if (accounting_any_prefix(name, measurements, sizeof(measurements) / sizeof(measurements[0])))
    {
        phase = ACCOUNTING_MEASUREMENT;
    }
    else if (accounting_any_prefix(name, uploads, sizeof(uploads) / sizeof(uploads[0])))
    {
        phase = ACCOUNTING_UPLOAD;
    }
    else if (accounting_any_prefix(name, overhead, sizeof(overhead) / sizeof(overhead[0])))
    {
        phase = ACCOUNTING_OVERHEAD;
    }
    else if (accounting_any_prefix(name, setup, sizeof(setup) / sizeof(setup[0])))
    {
        phase = ACCOUNTING_SETUP;
    }
    else if (accounting_any_prefix(name, correctness, sizeof(correctness) / sizeof(correctness[0])))
    {
        phase = ACCOUNTING_CORRECTNESS;
    }
    return phase;
}

static bool accounting_failure(size_t outcome)
{
    return outcome == 1 || outcome == 3 || outcome == 6 || outcome == 8;
}

static size_t accounting_outcome(const char* status, const char* conclusion)
{
    const size_t count = sizeof(accounting_outcomes) / sizeof(accounting_outcomes[0]);
    size_t result = strcmp(status, "completed") == 0 ? timing_name(conclusion, accounting_outcomes, count - 1) : count - 1;
    if (result == count - 1 && strcmp(status, "completed") == 0)
    {
        result = count - 2;
    }
    return result;
}

static int accounting_edge_compare(const void* left, const void* right)
{
    const AccountingEdge* a = left;
    const AccountingEdge* b = right;
    int result = (a->time > b->time) - (a->time < b->time);
    if (result == 0)
    {
        result = (a->delta > b->delta) - (a->delta < b->delta);
    }
    return result;
}

static int accounting_u64_compare(const void* left, const void* right)
{
    const uint64_t a = *(const uint64_t*)left;
    const uint64_t b = *(const uint64_t*)right;
    return (a > b) - (a < b);
}

static void accounting_interval(Accounting* accounting, size_t* count, uint64_t start, uint64_t end, AccountingPhase phase)
{
    if (start != BUSTER_ACCOUNTING_UNKNOWN && end != BUSTER_ACCOUNTING_UNKNOWN && end > start)
    {
        accounting->edges[*count] = (AccountingEdge){
            .time = start,
            .delta = 1,
            .phase = phase,
        };
        accounting->edges[*count + 1] = (AccountingEdge){
            .time = end,
            .delta = -1,
            .phase = phase,
        };
        *count += 2;
    }
}

static void accounting_job_finish(Accounting* accounting)
{
    if (accounting->current_job != SIZE_MAX)
    {
        AccountingJob* job = &accounting->jobs[accounting->current_job];
        const size_t step_count = accounting->step_count;
        uint64_t execution = BUSTER_ACCOUNTING_UNKNOWN;
        job->exact = job->allocated && job->terminal && accounting_duration(job->started, job->completed, &execution);
        job->view_start = job->started;
        job->view_end = job->exact ? job->completed : BUSTER_ACCOUNTING_UNKNOWN;
        bool bounded = job->exact;
        if (job->allocated && !job->terminal && job->started != BUSTER_ACCOUNTING_UNKNOWN && accounting->snapshot >= job->started)
        {
            job->view_end = accounting->snapshot;
            execution = accounting->snapshot - job->started;
            bounded = true;
        }
        size_t edges = 0;
        if (job->allocated)
        {
            for (size_t i = 0; i < step_count; i += 1)
            {
                const AccountingStep* step = &accounting->steps[i];
                uint64_t start = step->start;
                uint64_t end = step->end;
                if (bounded)
                {
                    start = start < job->view_start ? job->view_start : start;
                    end = end > job->view_end ? job->view_end : end;
                }
                accounting_interval(accounting, &edges, start, end, step->phase);
            }
            if (job->lock_seen)
            {
                accounting_duration(accounting->lock_start, accounting->lock_acquired, &job->lock_wait);
                accounting_duration(accounting->lock_acquired, accounting->lock_released, &job->lock_hold);
                uint64_t start = accounting->lock_start;
                uint64_t end = accounting->lock_acquired;
                if (bounded && job->lock_wait != BUSTER_ACCOUNTING_UNKNOWN)
                {
                    start = start < job->view_start ? job->view_start : start;
                    end = end > job->view_end ? job->view_end : end;
                }
                if (job->lock_wait != BUSTER_ACCOUNTING_UNKNOWN)
                {
                    accounting_interval(accounting, &edges, start, end, ACCOUNTING_LOCK_WAIT);
                }
            }
        }
        qsort(accounting->edges, edges, sizeof(accounting->edges[0]), accounting_edge_compare);
        unsigned int active[ACCOUNTING_PHASE_COUNT] = { 0 };
        unsigned int active_steps = 0;
        uint64_t previous = bounded ? job->view_start : (edges != 0 ? accounting->edges[0].time : 0);
        for (size_t i = 0; i < edges; i += 1)
        {
            const AccountingEdge edge = accounting->edges[i];
            if (edge.time > previous && (bounded || active_steps != 0 || active[ACCOUNTING_LOCK_WAIT] != 0))
            {
                AccountingPhase phase = ACCOUNTING_UNCLASSIFIED;
                if (active[ACCOUNTING_LOCK_WAIT] != 0)
                {
                    // The runner hook contains the lock wait. Carve it out
                    // rather than charging the same seconds twice.
                    phase = ACCOUNTING_LOCK_WAIT;
                }
                else if (active_steps > 1)
                {
                    phase = ACCOUNTING_OVERLAP;
                }
                else
                {
                    for (size_t j = 0; j < ACCOUNTING_PHASE_COUNT; j += 1)
                    {
                        if (active[j] != 0)
                        {
                            phase = (AccountingPhase)j;
                        }
                    }
                }
                job->phases[phase] += edge.time - previous;
            }
            if (edge.delta > 0)
            {
                active[edge.phase] += 1;
                active_steps += edge.phase != ACCOUNTING_LOCK_WAIT;
            }
            else
            {
                active[edge.phase] -= 1;
                active_steps -= edge.phase != ACCOUNTING_LOCK_WAIT;
            }
            previous = edge.time;
        }
        if (bounded && job->view_end > previous)
        {
            job->phases[ACCOUNTING_UNCLASSIFIED] += job->view_end - previous;
        }
        uint64_t reconciled = 0;
        for (size_t i = 0; i < ACCOUNTING_PHASE_COUNT; i += 1)
        {
            reconciled += job->phases[i];
        }
        job->observed = bounded || edges != 0;
        job->execution = bounded ? execution : (job->observed ? reconciled : 0);
        if (!job->allocated)
        {
            job->view_start = BUSTER_ACCOUNTING_UNKNOWN;
            job->view_end = BUSTER_ACCOUNTING_UNKNOWN;
        }
        else if (!bounded)
        {
            // Step-only evidence is a lower bound, not a continuous runner
            // interval. It must not manufacture occupation between steps.
            job->view_start = BUSTER_ACCOUNTING_UNKNOWN;
            job->view_end = BUSTER_ACCOUNTING_UNKNOWN;
        }
        if (job->allocated)
        {
            accounting_duration(job->created, job->started, &job->queue);
        }
        if (job->exact && accounting_failure(job->outcome))
        {
            job->feedback = job->feedback < job->completed ? job->feedback : job->completed;
        }
        job->steps = step_count;
        accounting->step_count = 0;
        accounting->current_job = SIZE_MAX;
    }
}

static bool accounting_meta_add(Accounting* accounting, char** fields, size_t count)
{
    bool valid = count == 5 && !accounting->meta && accounting->run_count == 0;
    if (valid)
    {
        valid = timing_number(fields[1], BUSTER_ACCOUNTING_TIME_MAX, &accounting->snapshot) &&
                accounting_optional(fields[2], BUSTER_ACCOUNTING_TIME_MAX, &accounting->split) &&
                accounting_optional(fields[3], 1000000, &accounting->capacity) &&
                accounting_copy(accounting->capacity_source, sizeof(accounting->capacity_source), fields[4]);
        valid = valid && (accounting->capacity == BUSTER_ACCOUNTING_UNKNOWN || (accounting->capacity != 0 && strcmp(fields[4], "-") != 0));
        accounting->meta = valid;
    }
    return valid;
}

static bool accounting_run_add(Accounting* accounting, char** fields, size_t count)
{
    bool valid = accounting->meta && count == 15 && accounting->run_count < BUSTER_ACCOUNTING_RUNS;
    if (valid)
    {
        accounting_job_finish(accounting);
        AccountingRun* run = &accounting->runs[accounting->run_count];
        uint64_t attempt = 0;
        uint64_t complete = 0;
        run->event = timing_name(fields[4], accounting_events, sizeof(accounting_events) / sizeof(accounting_events[0]));
        run->cohort = timing_name(fields[5], accounting_cohorts, sizeof(accounting_cohorts) / sizeof(accounting_cohorts[0]));
        run->fate = timing_name(fields[13], accounting_fates, sizeof(accounting_fates) / sizeof(accounting_fates[0]));
        run->terminal = strcmp(fields[8], "completed") == 0;
        run->outcome = accounting_outcome(fields[8], fields[9]);
        valid = timing_number(fields[1], UINT64_MAX, &run->id) && run->id != 0 && timing_number(fields[2], 1000000, &attempt) && attempt != 0 &&
                run->event < sizeof(accounting_events) / sizeof(accounting_events[0]) && run->cohort < 3 && run->fate < sizeof(accounting_fates) / sizeof(accounting_fates[0]) &&
                accounting_copy(run->workflow, sizeof(run->workflow), fields[3]) && accounting_copy(run->sha, sizeof(run->sha), fields[6]) &&
                accounting_copy(run->branch, sizeof(run->branch), fields[7]) && accounting_optional(fields[10], BUSTER_ACCOUNTING_TIME_MAX, &run->created) &&
                accounting_optional(fields[11], BUSTER_ACCOUNTING_TIME_MAX, &run->started) && accounting_optional(fields[12], BUSTER_ACCOUNTING_TIME_MAX, &run->updated) &&
                timing_number(fields[14], 1, &complete);
        const size_t sha_length = strlen(fields[6]);
        valid = valid && (strcmp(fields[6], "-") == 0 || sha_length == 40);
        if (valid)
        {
            const size_t expected_cohort = accounting->split == BUSTER_ACCOUNTING_UNKNOWN || run->created == BUSTER_ACCOUNTING_UNKNOWN ? 0 : (run->created < accounting->split ? 1 : 2);
            valid = run->cohort == expected_cohort;
        }
        for (size_t i = 0; i < sha_length && sha_length == 40 && valid; i += 1)
        {
            const char c = fields[6][i];
            valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }
        for (size_t i = 0; i < accounting->run_count && valid; i += 1)
        {
            valid = accounting->runs[i].id != run->id || accounting->runs[i].attempt != attempt;
            if (accounting->runs[i].id == run->id)
            {
                const AccountingRun* previous = &accounting->runs[i];
                valid = valid && previous->event == run->event && previous->cohort == run->cohort && strcmp(previous->workflow, run->workflow) == 0 && strcmp(previous->sha, run->sha) == 0;
            }
        }
        if (valid)
        {
            run->attempt = (uint32_t)attempt;
            run->jobs_complete = complete != 0;
            run->first_job = accounting->job_count;
            accounting->current_run = accounting->run_count;
            accounting->run_count += 1;
        }
    }
    return valid;
}

static bool accounting_job_add(Accounting* accounting, char** fields, size_t count)
{
    bool valid = accounting->current_run != SIZE_MAX && count == 12 && accounting->job_count < BUSTER_ACCOUNTING_JOBS;
    if (valid)
    {
        accounting_job_finish(accounting);
        AccountingJob* job = &accounting->jobs[accounting->job_count];
        uint64_t complete = 0;
        job->run = accounting->current_run;
        job->runner_class = timing_name(fields[3], accounting_classes, sizeof(accounting_classes) / sizeof(accounting_classes[0]));
        job->terminal = strcmp(fields[6], "completed") == 0;
        job->outcome = accounting_outcome(fields[6], fields[7]);
        job->queue = BUSTER_ACCOUNTING_UNKNOWN;
        job->feedback = BUSTER_ACCOUNTING_UNKNOWN;
        job->lock_wait = BUSTER_ACCOUNTING_UNKNOWN;
        job->lock_hold = BUSTER_ACCOUNTING_UNKNOWN;
        valid = timing_number(fields[1], UINT64_MAX, &job->id) && job->id != 0 &&
                accounting_copy(job->name, sizeof(job->name), fields[2]) && job->runner_class < 4 &&
                accounting_copy(job->labels, sizeof(job->labels), fields[4]) && accounting_copy(job->cpu, sizeof(job->cpu), fields[5]) &&
                accounting_optional(fields[8], BUSTER_ACCOUNTING_TIME_MAX, &job->created) && accounting_optional(fields[9], BUSTER_ACCOUNTING_TIME_MAX, &job->started) &&
                accounting_optional(fields[10], BUSTER_ACCOUNTING_TIME_MAX, &job->completed) && timing_number(fields[11], 1, &complete);
        if (valid)
        {
            job->steps_complete = complete != 0;
            job->allocated = job->runner_class != 2 && job->outcome != 4;
            accounting->current_job = accounting->job_count;
            accounting->job_count += 1;
            accounting->runs[accounting->current_run].jobs += 1;
            accounting->lock_start = BUSTER_ACCOUNTING_UNKNOWN;
            accounting->lock_acquired = BUSTER_ACCOUNTING_UNKNOWN;
            accounting->lock_released = BUSTER_ACCOUNTING_UNKNOWN;
        }
    }
    return valid;
}

static bool accounting_step_add(Accounting* accounting, char** fields, size_t count)
{
    bool valid = accounting->current_job != SIZE_MAX && count == 7 && accounting->step_count < BUSTER_ACCOUNTING_STEPS;
    if (valid)
    {
        AccountingJob* job = &accounting->jobs[accounting->current_job];
        AccountingStep* step = &accounting->steps[accounting->step_count];
        valid = timing_number(fields[1], UINT32_MAX, &step->number) && step->number != 0 && strlen(fields[2]) < BUSTER_ACCOUNTING_TEXT &&
                accounting_optional(fields[5], BUSTER_ACCOUNTING_TIME_MAX, &step->start) && accounting_optional(fields[6], BUSTER_ACCOUNTING_TIME_MAX, &step->end);
        for (size_t i = 0; i < accounting->step_count && valid; i += 1)
        {
            valid = accounting->steps[i].number != step->number;
        }
        if (valid)
        {
            step->phase = accounting_phase(fields[2]);
            const size_t outcome = accounting_outcome(fields[3], fields[4]);
            uint64_t duration = 0;
            const bool interval = accounting_duration(step->start, step->end, &duration);
            if (outcome == 4)
            {
                // GitHub may put placeholder or reversed dates on skipped
                // steps; those dates never mean the step consumed resources.
                step->start = BUSTER_ACCOUNTING_UNKNOWN;
                step->end = BUSTER_ACCOUNTING_UNKNOWN;
            }
            else if (!interval && strcmp(fields[3], "in_progress") == 0 && step->start != BUSTER_ACCOUNTING_UNKNOWN && accounting->snapshot >= step->start)
            {
                step->end = accounting->snapshot;
                step->partial = true;
                job->partial_steps += 1;
            }
            else if (!interval)
            {
                step->start = BUSTER_ACCOUNTING_UNKNOWN;
                step->end = BUSTER_ACCOUNTING_UNKNOWN;
                job->missing_steps += 1;
            }
            const bool endpoint_in_job = (job->started == BUSTER_ACCOUNTING_UNKNOWN || step->end >= job->started) &&
                                         (job->completed == BUSTER_ACCOUNTING_UNKNOWN || step->end <= job->completed);
            if (job->allocated && interval && endpoint_in_job && outcome != 4 && outcome != 2 && outcome != 10 &&
                (accounting_failure(outcome) || step->phase == ACCOUNTING_CORRECTNESS || step->phase == ACCOUNTING_MEASUREMENT))
            {
                job->feedback = job->feedback < step->end ? job->feedback : step->end;
            }
            accounting->step_count += 1;
        }
    }
    return valid;
}

static bool accounting_add(Accounting* accounting, const char* input)
{
    const size_t length = strlen(input);
    bool valid = length != 0 && length < BUSTER_ACCOUNTING_LINE && input[length - 1] == '\n';
    char line[BUSTER_ACCOUNTING_LINE];
    char* fields[20] = { NULL };
    size_t count = 1;
    if (valid)
    {
        memcpy(line, input, length);
        line[length - 1] = 0;
        fields[0] = line;
        for (size_t i = 0; i + 1 < length && valid; i += 1)
        {
            if (line[i] == '\t')
            {
                valid = count < sizeof(fields) / sizeof(fields[0]);
                if (valid)
                {
                    line[i] = 0;
                    fields[count++] = line + i + 1;
                }
            }
            else
            {
                valid = (unsigned char)line[i] >= 32 && line[i] != 127;
            }
        }
    }
    if (valid)
    {
        if (strcmp(fields[0], "meta") == 0)
        {
            valid = accounting_meta_add(accounting, fields, count);
        }
        else if (strcmp(fields[0], "run") == 0)
        {
            valid = accounting_run_add(accounting, fields, count);
        }
        else if (strcmp(fields[0], "job") == 0)
        {
            valid = accounting_job_add(accounting, fields, count);
        }
        else if (strcmp(fields[0], "step") == 0)
        {
            valid = accounting_step_add(accounting, fields, count);
        }
        else if (strcmp(fields[0], "lock") == 0)
        {
            valid = count == 4 && accounting->current_job != SIZE_MAX;
            if (valid)
            {
                AccountingJob* job = &accounting->jobs[accounting->current_job];
                valid = !job->lock_seen && accounting_optional(fields[1], BUSTER_ACCOUNTING_TIME_MAX, &accounting->lock_start) &&
                        accounting_optional(fields[2], BUSTER_ACCOUNTING_TIME_MAX, &accounting->lock_acquired) && accounting_optional(fields[3], BUSTER_ACCOUNTING_TIME_MAX, &accounting->lock_released);
                job->lock_seen = valid;
            }
        }
        else if (strcmp(fields[0], "note") == 0)
        {
            valid = accounting->meta && count == 3 && accounting->note_count < BUSTER_ACCOUNTING_NOTES;
            if (valid)
            {
                AccountingNote* note = &accounting->notes[accounting->note_count];
                valid = accounting_copy(note->source, sizeof(note->source), fields[1]) && accounting_copy(note->text, sizeof(note->text), fields[2]);
                accounting->note_count += valid;
            }
        }
        else
        {
            valid = false;
        }
    }
    return valid;
}

static Accounting* accounting_create(void)
{
    Accounting* accounting = calloc(1, sizeof(Accounting));
    if (accounting != NULL)
    {
        accounting->runs = calloc(BUSTER_ACCOUNTING_RUNS, sizeof(AccountingRun));
        accounting->jobs = calloc(BUSTER_ACCOUNTING_JOBS, sizeof(AccountingJob));
        accounting->logical = calloc(BUSTER_ACCOUNTING_RUNS, sizeof(AccountingLogical));
        accounting->groups = calloc(BUSTER_ACCOUNTING_GROUPS, sizeof(AccountingGroup));
        accounting->steps = calloc(BUSTER_ACCOUNTING_STEPS, sizeof(AccountingStep));
        accounting->edges = calloc(BUSTER_ACCOUNTING_JOBS * 2 + BUSTER_ACCOUNTING_STEPS * 2 + 4, sizeof(AccountingEdge));
        accounting->notes = calloc(BUSTER_ACCOUNTING_NOTES, sizeof(AccountingNote));
        accounting->ids = calloc(BUSTER_ACCOUNTING_JOBS, sizeof(uint64_t));
        accounting->values = calloc(BUSTER_ACCOUNTING_JOBS, sizeof(uint64_t));
        accounting->current_run = SIZE_MAX;
        accounting->current_job = SIZE_MAX;
    }
    return accounting;
}

static bool accounting_allocated(const Accounting* accounting)
{
    return accounting != NULL && accounting->runs != NULL && accounting->jobs != NULL && accounting->logical != NULL && accounting->groups != NULL &&
           accounting->steps != NULL && accounting->edges != NULL && accounting->notes != NULL && accounting->ids != NULL && accounting->values != NULL;
}

static void accounting_destroy(Accounting* accounting)
{
    if (accounting != NULL)
    {
        free(accounting->runs);
        free(accounting->jobs);
        free(accounting->logical);
        free(accounting->groups);
        free(accounting->steps);
        free(accounting->edges);
        free(accounting->notes);
        free(accounting->ids);
        free(accounting->values);
        free(accounting);
    }
}

static size_t accounting_group(Accounting* accounting, const AccountingRun* run)
{
    size_t result = accounting->group_count;
    for (size_t i = 0; i < accounting->group_count; i += 1)
    {
        const AccountingGroup* group = &accounting->groups[i];
        if (group->event == run->event && group->cohort == run->cohort && strcmp(group->workflow, run->workflow) == 0)
        {
            result = i;
        }
    }
    if (result == accounting->group_count && result < BUSTER_ACCOUNTING_GROUPS)
    {
        AccountingGroup* group = &accounting->groups[result];
        group->event = run->event;
        group->cohort = run->cohort;
        memcpy(group->workflow, run->workflow, sizeof(group->workflow));
        accounting->group_count += 1;
    }
    return result;
}

static bool accounting_finish(Accounting* accounting)
{
    accounting_job_finish(accounting);
    bool valid = accounting->meta;
    for (size_t i = 0; i < accounting->job_count; i += 1)
    {
        accounting->ids[i] = accounting->jobs[i].id;
        uint64_t phases = 0;
        for (size_t phase = 0; phase < ACCOUNTING_PHASE_COUNT; phase += 1)
        {
            phases += accounting->jobs[i].phases[phase];
        }
        valid = valid && phases == accounting->jobs[i].execution;
    }
    qsort(accounting->ids, accounting->job_count, sizeof(accounting->ids[0]), accounting_u64_compare);
    for (size_t i = 1; i < accounting->job_count && valid; i += 1)
    {
        valid = accounting->ids[i] != accounting->ids[i - 1];
    }
    for (size_t i = 0; i < accounting->run_count && valid; i += 1)
    {
        AccountingRun* run = &accounting->runs[i];
        run->group = accounting_group(accounting, run);
        valid = run->group < BUSTER_ACCOUNTING_GROUPS;
        size_t logical = accounting->logical_count;
        for (size_t j = 0; j < accounting->logical_count; j += 1)
        {
            if (accounting->logical[j].id == run->id)
            {
                logical = j;
            }
        }
        run->logical = logical;
        AccountingLogical* observation = &accounting->logical[logical];
        if (logical == accounting->logical_count)
        {
            observation->id = run->id;
            observation->latest = i;
            observation->group = run->group;
            observation->created = run->created;
            observation->feedback = BUSTER_ACCOUNTING_UNKNOWN;
            observation->completion = BUSTER_ACCOUNTING_UNKNOWN;
            observation->queue_max = BUSTER_ACCOUNTING_UNKNOWN;
            observation->tail = SIZE_MAX;
            observation->longest = SIZE_MAX;
            observation->complete = true;
            observation->classes_known = true;
            accounting->logical_count += 1;
        }
        if (accounting->runs[observation->latest].attempt < run->attempt)
        {
            observation->latest = i;
        }
        observation->created = observation->created < run->created ? observation->created : run->created;
        observation->attempts += 1;
        observation->complete = observation->complete && run->jobs_complete;
    }
    for (size_t i = 0; i < accounting->job_count && valid; i += 1)
    {
        const AccountingJob* job = &accounting->jobs[i];
        const AccountingRun* run = &accounting->runs[job->run];
        AccountingLogical* logical = &accounting->logical[run->logical];
        logical->jobs += 1;
        logical->resources[job->runner_class] += job->execution;
        logical->retry += run->attempt > 1 ? job->execution : 0;
        logical->unknown_jobs += job->allocated && !job->observed;
        logical->partial_jobs += job->allocated && job->observed && !job->exact;
        logical->complete = logical->complete && (!job->allocated || job->exact);
        logical->classes_known = logical->classes_known && (!job->allocated || job->runner_class != 3);
        logical->feedback = logical->feedback < job->feedback ? logical->feedback : job->feedback;
        if (job->queue != BUSTER_ACCOUNTING_UNKNOWN && (logical->queue_max == BUSTER_ACCOUNTING_UNKNOWN || job->queue > logical->queue_max))
        {
            logical->queue_max = job->queue;
        }
        if (job->observed && (logical->longest == SIZE_MAX || job->execution > accounting->jobs[logical->longest].execution))
        {
            logical->longest = i;
        }
        if (job->run == logical->latest && job->allocated && job->terminal && job->completed != BUSTER_ACCOUNTING_UNKNOWN &&
            (logical->tail == SIZE_MAX || job->completed > accounting->jobs[logical->tail].completed))
        {
            logical->tail = i;
        }
    }
    for (size_t i = 0; i < accounting->logical_count && valid; i += 1)
    {
        AccountingLogical* logical = &accounting->logical[i];
        const AccountingRun* latest = &accounting->runs[logical->latest];
        logical->complete = logical->complete && logical->attempts == latest->attempt;
        bool latest_complete = latest->jobs_complete && latest->terminal && logical->tail != SIZE_MAX;
        for (size_t j = latest->first_job; j < latest->first_job + latest->jobs; j += 1)
        {
            const AccountingJob* job = &accounting->jobs[j];
            latest_complete = latest_complete && (!job->allocated || (job->terminal && job->exact));
        }
        if (latest_complete)
        {
            accounting_duration(logical->created, accounting->jobs[logical->tail].completed, &logical->completion);
        }
        uint64_t feedback = BUSTER_ACCOUNTING_UNKNOWN;
        accounting_duration(logical->created, logical->feedback, &feedback);
        logical->feedback = feedback;
        logical->wall_complete = logical->complete && logical->completion != BUSTER_ACCOUNTING_UNKNOWN;
        if (logical->wall_complete)
        {
            size_t edges = 0;
            const uint64_t end = logical->created + logical->completion;
            for (size_t j = 0; j < accounting->job_count; j += 1)
            {
                const AccountingJob* job = &accounting->jobs[j];
                if (accounting->runs[job->run].logical == i && job->view_start != BUSTER_ACCOUNTING_UNKNOWN && job->view_end != BUSTER_ACCOUNTING_UNKNOWN)
                {
                    const uint64_t start = job->view_start < logical->created ? logical->created : job->view_start;
                    const uint64_t finish = job->view_end > end ? end : job->view_end;
                    accounting_interval(accounting, &edges, start, finish, ACCOUNTING_OVERHEAD);
                }
            }
            qsort(accounting->edges, edges, sizeof(accounting->edges[0]), accounting_edge_compare);
            size_t active = 0;
            uint64_t previous = logical->created;
            for (size_t j = 0; j < edges; j += 1)
            {
                const AccountingEdge edge = accounting->edges[j];
                logical->union_seconds += active != 0 ? edge.time - previous : 0;
                active = edge.delta > 0 ? active + 1 : active - 1;
                previous = edge.time;
            }
        }
    }
    return valid;
}

static void accounting_text(FILE* output, const char* text)
{
    // Names and logs are data. Do not allow them to alter Markdown tables or
    // inject HTML, links, images or code into the published report.
    for (size_t i = 0; text[i] != 0; i += 1)
    {
        const char c = text[i];
        if (c == '&')
        {
            fputs("&amp;", output);
        }
        else if (c == '<')
        {
            fputs("&lt;", output);
        }
        else if (c == '>')
        {
            fputs("&gt;", output);
        }
        else if (c == '|')
        {
            fputs("&#124;", output);
        }
        else
        {
            if (c == '\\' || c == '`' || c == '[' || c == ']' || c == '*' || c == '_')
            {
                fputc('\\', output);
            }
            fputc(c, output);
        }
    }
}

static void accounting_quantiles(FILE* output, uint64_t* values, size_t count)
{
    if (count == 0)
    {
        fputs("unknown | unknown", output);
    }
    else
    {
        qsort(values, count, sizeof(values[0]), accounting_u64_compare);
        const double median = (double)values[(count - 1) / 2] + ((double)values[count / 2] - (double)values[(count - 1) / 2]) / 2;
        fprintf(output, "%.1f | %llu", median, (unsigned long long)values[(count * 95 + 99) / 100 - 1]);
    }
}

static uint64_t accounting_resources(const AccountingLogical* run)
{
    return run->resources[0] + run->resources[1] + run->resources[3];
}

static void accounting_latency_report(Accounting* accounting, FILE* output)
{
    fputs("## Feedback, completion and consumption\n\n", output);
    fputs("One sample per workflow run, with every collected attempt charged to it. Quantiles use available observations across **all outcomes**, not just successful runs; `known / runs` exposes missing feedback and censored tails. p50 is the median and p95 is nearest rank.\n\n", output);
    fputs("First useful feedback is the first observed completed correctness/measurement step, or an actionable step/job failure. Checkout and successful setup alone do not count. Completion is the last allocated job's completion in a fully observed terminal latest attempt: a **job-completion proxy**, never the mutable run `updated_at`.\n\n", output);
    fputs("| Workflow | Event | Cohort | Metric | Known / runs | p50 (s) | p95 (s) |\n| --- | --- | --- | --- | ---: | ---: | ---: |\n", output);
    for (size_t group = 0; group < accounting->group_count; group += 1)
    {
        const AccountingGroup* key = &accounting->groups[group];
        for (size_t metric = 0; metric < 3; metric += 1)
        {
            size_t samples = 0;
            size_t runs = 0;
            for (size_t i = 0; i < accounting->logical_count; i += 1)
            {
                const AccountingLogical* run = &accounting->logical[i];
                if (run->group == group)
                {
                    runs += 1;
                    const uint64_t value = metric == 0 ? run->feedback : metric == 1 ? run->completion : run->queue_max;
                    if (value != BUSTER_ACCOUNTING_UNKNOWN)
                    {
                        accounting->values[samples++] = value;
                    }
                }
            }
            fputs("| ", output);
            accounting_text(output, key->workflow);
            fprintf(output, " | %s | %s | %s | %zu / %zu | ", accounting_events[key->event], accounting_cohorts[key->cohort], metric == 0 ? "first useful feedback" : metric == 1 ? "completion proxy" : "longest job queue wait", samples, runs);
            accounting_quantiles(output, accounting->values, samples);
            fputs(" |\n", output);
        }
    }
    fputs("\n| Workflow | Event | Cohort | Runs | Attempts seen / expected | Complete resource records | Available runner-min | Hosted runner-min | Retry runner-min | Distinct head SHAs | Runner-min / sampled SHA |\n| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n", output);
    for (size_t group = 0; group < accounting->group_count; group += 1)
    {
        const AccountingGroup* key = &accounting->groups[group];
        size_t runs = 0;
        size_t attempts = 0;
        size_t expected = 0;
        size_t complete = 0;
        size_t revisions = 0;
        uint64_t runner = 0;
        uint64_t hosted = 0;
        uint64_t retry = 0;
        for (size_t i = 0; i < accounting->logical_count; i += 1)
        {
            const AccountingLogical* run = &accounting->logical[i];
            const AccountingRun* latest = &accounting->runs[run->latest];
            if (run->group == group)
            {
                runs += 1;
                attempts += run->attempts;
                expected += latest->attempt;
                complete += run->complete;
                runner += accounting_resources(run);
                hosted += run->resources[0];
                retry += run->retry;
                bool first = strcmp(latest->sha, "-") != 0;
                for (size_t j = 0; j < i && first; j += 1)
                {
                    const AccountingLogical* previous = &accounting->logical[j];
                    first = previous->group != group || strcmp(accounting->runs[previous->latest].sha, latest->sha) != 0;
                }
                revisions += first;
            }
        }
        fputs("| ", output);
        accounting_text(output, key->workflow);
        fprintf(output, " | %s | %s | %zu | %zu / %zu | %zu / %zu | %.3f | %.3f | %.3f | %zu | ", accounting_events[key->event], accounting_cohorts[key->cohort], runs, attempts, expected, complete, runs, (double)runner / 60, (double)hosted / 60, (double)retry / 60, revisions);
        if (revisions != 0)
        {
            fprintf(output, "%.3f", (double)runner / 60 / (double)revisions);
        }
        else
        {
            fputs("unknown", output);
        }
        fputs(" |\n", output);
    }
    fputs("\nAvailable resource totals include elapsed and step-only lower bounds; missing jobs/attempts are **not zero cost**. Retry work is the observed work of attempts greater than one, already included in the total. Per-SHA ratios describe the sampled head SHAs, not unobserved revisions or marginal change cost. Before/after cohorts must have comparable workflow/event/CPU coverage; bounded latest-run samples do not establish an arrival rate or a causal speedup.\n", output);
    fputs("\n| Workflow | Event | Cohort | Latest outcome | Runs |\n| --- | --- | --- | --- | ---: |\n", output);
    for (size_t group = 0; group < accounting->group_count; group += 1)
    {
        const AccountingGroup* key = &accounting->groups[group];
        for (size_t outcome = 0; outcome < sizeof(accounting_outcomes) / sizeof(accounting_outcomes[0]); outcome += 1)
        {
            size_t count = 0;
            for (size_t i = 0; i < accounting->logical_count; i += 1)
            {
                const AccountingLogical* logical = &accounting->logical[i];
                count += logical->group == group && accounting->runs[logical->latest].outcome == outcome;
            }
            if (count != 0)
            {
                fputs("| ", output);
                accounting_text(output, key->workflow);
                fprintf(output, " | %s | %s | %s | %zu |\n", accounting_events[key->event], accounting_cohorts[key->cohort], accounting_outcomes[outcome], count);
            }
        }
    }
}

static void accounting_phase_report(Accounting* accounting, FILE* output)
{
    fputs("\n## Reconciled job phases\n\n", output);
    fputs("Each allocated job is partitioned once. Step spans are clipped to its observed execution interval; conflicting spans go to `overlap`. Missing steps and gaps go to `unclassified`. Skipped steps and unallocated/skipped jobs consume no reported execution. Setup/build is the sum of actual setup/build spans and does not depend on a native-test endpoint. `Compare`, `Time` and `Gauge` count with `Benchmark`, `Measure`, `Attribute`, `Profile` and the two Zen 5 sample reproduction steps.\n\n", output);
    fputs("Classification uses whole step names. A combined build-and-qualify step stays in one bucket; these are administrative phase groups, not instrumentation of commands inside a step. Unknown/new names remain unclassified.\n\n", output);
    fputs("| Runner class | Cohort | Observation | Jobs | Execution (s) | setup/build | correctness | measurement | uploads/reporting | other overhead | lock wait | unclassified | overlap | Residual (s) |\n| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n", output);
    size_t missing_steps = 0;
    size_t missing_lists = 0;
    size_t partial_steps = 0;
    size_t unknown = 0;
    size_t unallocated = 0;
    for (size_t i = 0; i < accounting->job_count; i += 1)
    {
        const AccountingJob* job = &accounting->jobs[i];
        missing_steps += job->missing_steps;
        missing_lists += !job->steps_complete;
        partial_steps += job->partial_steps;
        unknown += job->allocated && !job->observed;
        unallocated += !job->allocated;
    }
    for (size_t runner_class = 0; runner_class < 4; runner_class += 1)
    {
        for (size_t cohort = 0; cohort < 3; cohort += 1)
        {
            for (size_t exact = 0; exact < 2; exact += 1)
            {
                size_t count = 0;
                uint64_t execution = 0;
                uint64_t phases[ACCOUNTING_PHASE_COUNT] = { 0 };
                for (size_t i = 0; i < accounting->job_count; i += 1)
                {
                    const AccountingJob* job = &accounting->jobs[i];
                    if (job->runner_class == runner_class && accounting->runs[job->run].cohort == cohort && job->observed && job->exact == (exact != 0))
                    {
                        count += 1;
                        execution += job->execution;
                        for (size_t phase = 0; phase < ACCOUNTING_PHASE_COUNT; phase += 1)
                        {
                            phases[phase] += job->phases[phase];
                        }
                    }
                }
                if (count != 0)
                {
                    fprintf(output, "| %s | %s | %s | %zu | %llu", accounting_classes[runner_class], accounting_cohorts[cohort], exact != 0 ? "complete interval" : "partial lower bound", count, (unsigned long long)execution);
                    uint64_t sum = 0;
                    for (size_t phase = 0; phase < ACCOUNTING_PHASE_COUNT; phase += 1)
                    {
                        fprintf(output, " | %llu", (unsigned long long)phases[phase]);
                        sum += phases[phase];
                    }
                    fprintf(output, " | %llu |\n", (unsigned long long)(execution - sum));
                }
            }
        }
    }
    fprintf(output, "\nUnknown execution: %zu allocated/possibly allocated jobs. Unallocated/skipped jobs: %zu. Missing step intervals: %zu; unavailable step lists: %zu; currently running step intervals: %zu. These counts remain visible even when an available total reconciles.\n", unknown, unallocated, missing_steps, missing_lists, partial_steps);
    size_t waits = 0;
    size_t holds = 0;
    uint64_t wait_seconds = 0;
    uint64_t hold_seconds = 0;
    for (size_t i = 0; i < accounting->job_count; i += 1)
    {
        const AccountingJob* job = &accounting->jobs[i];
        if (job->lock_wait != BUSTER_ACCOUNTING_UNKNOWN)
        {
            waits += 1;
            wait_seconds += job->lock_wait;
        }
        if (job->lock_hold != BUSTER_ACCOUNTING_UNKNOWN)
        {
            holds += 1;
            hold_seconds += job->lock_hold;
        }
    }
    fprintf(output, "\nLock-log observations: wait %llu s in %zu jobs; hold %llu s in %zu jobs. A missing log/acquisition/release is unknown. Hold is an overlapping occupation metric, **not added** to execution phases. Whole-second logs and API timestamps can differ at the boundaries; the additive wait is clipped to the job.\n", (unsigned long long)wait_seconds, waits, (unsigned long long)hold_seconds, holds);
}

static void accounting_class_report(Accounting* accounting, FILE* output)
{
    fputs("\n## Observed runner classes and CPUs\n\n", output);
    fputs("CPU model comes from a job's log when available. Runner labels, CPU models and observed peak are not the account's configured concurrency limit. Unknown CPU/model/class stays explicit.\n\n", output);
    fputs("| Class | Labels | CPU model | Cohort | Jobs | Complete intervals | Available runner-min |\n| --- | --- | --- | --- | ---: | ---: | ---: |\n", output);
    for (size_t i = 0; i < accounting->job_count; i += 1)
    {
        const AccountingJob* key = &accounting->jobs[i];
        if (key->allocated)
        {
            bool first = true;
            const size_t cohort = accounting->runs[key->run].cohort;
            for (size_t j = 0; j < i && first; j += 1)
            {
                const AccountingJob* previous = &accounting->jobs[j];
                first = !previous->allocated || previous->runner_class != key->runner_class || accounting->runs[previous->run].cohort != cohort ||
                        strcmp(previous->labels, key->labels) != 0 || strcmp(previous->cpu, key->cpu) != 0;
            }
            if (first)
            {
                size_t count = 0;
                size_t complete = 0;
                uint64_t execution = 0;
                for (size_t j = i; j < accounting->job_count; j += 1)
                {
                    const AccountingJob* job = &accounting->jobs[j];
                    if (job->allocated && job->runner_class == key->runner_class && accounting->runs[job->run].cohort == cohort && strcmp(job->labels, key->labels) == 0 && strcmp(job->cpu, key->cpu) == 0)
                    {
                        count += 1;
                        complete += job->exact;
                        execution += job->execution;
                    }
                }
                fprintf(output, "| %s | ", accounting_classes[key->runner_class]);
                accounting_text(output, key->labels);
                fputs(" | ", output);
                accounting_text(output, strcmp(key->cpu, "-") == 0 ? "unknown" : key->cpu);
                fprintf(output, " | %s | %zu | %zu | %.3f |\n", accounting_cohorts[cohort], count, complete, (double)execution / 60);
            }
        }
    }
}

static size_t accounting_peak(Accounting* accounting)
{
    size_t edges = 0;
    for (size_t i = 0; i < accounting->job_count; i += 1)
    {
        const AccountingJob* job = &accounting->jobs[i];
        if (job->runner_class == 0)
        {
            accounting_interval(accounting, &edges, job->view_start, job->view_end, ACCOUNTING_OVERHEAD);
        }
    }
    qsort(accounting->edges, edges, sizeof(accounting->edges[0]), accounting_edge_compare);
    size_t active = 0;
    size_t peak = 0;
    for (size_t i = 0; i < edges; i += 1)
    {
        active = accounting->edges[i].delta > 0 ? active + 1 : active - 1;
        peak = active > peak ? active : peak;
    }
    return peak;
}

static void accounting_capacity_report(Accounting* accounting, FILE* output)
{
    fputs("\n## Merge queue, discarded work and finite capacity\n\n", output);
    fputs("Queue fate and test outcome are independent. A successful candidate may still be live. `invalidated` requires a deleted/moved candidate branch **and** an observed commit not on main; an unreadable API gives `unknown`. Invalidated work is discarded validation (rebuild/drop), not proof that a failure caused a rebuild.\n\n", output);
    fputs("| Fate | Queue runs | Available hosted runner-min | Available total runner-min |\n| --- | ---: | ---: | ---: |\n", output);
    size_t merged = 0;
    size_t complete = 0;
    size_t queue_count = 0;
    uint64_t all_hosted = 0;
    uint64_t complete_hosted = 0;
    uint64_t complete_wall = 0;
    uint64_t possible_superseded = 0;
    size_t possible_superseded_runs = 0;
    for (size_t fate = 0; fate < sizeof(accounting_fates) / sizeof(accounting_fates[0]); fate += 1)
    {
        size_t count = 0;
        uint64_t hosted = 0;
        uint64_t resources = 0;
        for (size_t i = 0; i < accounting->logical_count; i += 1)
        {
            const AccountingLogical* logical = &accounting->logical[i];
            const AccountingRun* latest = &accounting->runs[logical->latest];
            if (latest->event == 1 && latest->fate == fate)
            {
                count += 1;
                hosted += logical->resources[0];
                resources += accounting_resources(logical);
                merged += fate == 1;
                queue_count += 1;
                if (logical->complete && logical->classes_known && logical->completion != BUSTER_ACCOUNTING_UNKNOWN && latest->terminal)
                {
                    complete += 1;
                    complete_hosted += logical->resources[0];
                    complete_wall += logical->completion;
                }
            }
            if (fate == 5 && latest->event == 0 && latest->fate == fate)
            {
                possible_superseded += accounting_resources(logical);
                possible_superseded_runs += 1;
            }
        }
        if (count != 0)
        {
            fprintf(output, "| %s | %zu | %.3f | %.3f |\n", accounting_fates[fate], count, (double)hosted / 60, (double)resources / 60);
        }
        all_hosted += hosted;
    }
    if (queue_count == 0)
    {
        fputs("| no observations | 0 | unknown | unknown |\n", output);
    }
    if (merged != 0)
    {
        fprintf(output, "\nAll sampled queue work / observed merged candidates: %.3f hosted runner-min per merge (%zu observed merges). This window ratio includes discarded work and retries; incomplete or unmatched cohorts make it a lower-bound accounting ratio, not an isolated candidate cost.\n", (double)all_hosted / 60 / (double)merged, merged);
    }
    else
    {
        fputs("\nRunner-minutes per merge: unknown; no merged candidate was observed.\n", output);
    }
    fprintf(output, "\nPotential supersession: %zu cancelled older PR runs have newer same-PR/workflow observations, consuming %.3f available runner-min. Cancellation cause is not in the Actions snapshot, so **confirmed superseded waste is unknown**. Failure-attributed rebuild work is also unknown without queue lifecycle evidence; discarded candidate work and observed retry work are reported separately above.\n", possible_superseded_runs, (double)possible_superseded / 60);
    fprintf(output, "\nObserved peak simultaneous hosted jobs: **%zu**, a lower bound on capacity exercised by this bounded repository sample, not the organization/account limit.\n", accounting_peak(accounting));
    if (accounting->capacity == BUSTER_ACCOUNTING_UNKNOWN)
    {
        fputs("\nActual configured account hosted concurrency: **unknown**. Published plan defaults and observed runner peaks are not an administrator's record. Supply the verified limit and its dated source before recommending numeric admission changes.\n", output);
    }
    else
    {
        fprintf(output, "\nRecorded account hosted concurrency: **%llu slots**. Source: ", (unsigned long long)accounting->capacity);
        accounting_text(output, accounting->capacity_source);
        fputs(". This is an input record, not an API verification by this report. Runner-class limits and other repositories' consumption still apply.\n", output);
    }
    fputs("\nFor `C_available` eligible runner slots, `W` hosted runner-minutes per candidate, `T` candidate critical-path/completion minutes, and `K` in-flight candidates, a necessary ceiling is `min(60*C_available/W, 60*K/T)` candidates/hour. Stability requires arrival rate below that ceiling **with headroom**. `C_available` must subtract simultaneous PR, nightly, preparation, reporting and other-repository demand; this latest-run sample is not an exhaustive account utilization window. Per-workflow `max-parallel` does not reserve account slots.\n", output);
    if (complete != 0 && complete_hosted != 0 && complete_wall != 0)
    {
        const double work = (double)complete_hosted / 60 / (double)complete;
        const double wall = (double)complete_wall / 60 / (double)complete;
        fprintf(output, "\nThe %zu fully observed terminal candidates with known runner classes average W = %.3f hosted runner-min and T = %.3f min (all outcomes). The corresponding **sample-based upper-bound model**, not a safe admission setting, is `min(%.4f * C_available, %.4f * K)` candidates/hour. Successful and failed candidates can have very different work; rerun this comparison by matched cohort before tuning.\n", complete, work, wall, 60 / work, 60 / wall);
        if (accounting->capacity != BUSTER_ACCOUNTING_UNKNOWN)
        {
            fprintf(output, "\nEven if every recorded account slot were available, the resource ceiling would be %.3f candidates/hour; other demand and critical-path limits can only lower it.\n", 60 * (double)accounting->capacity / work);
        }
    }
    else
    {
        fputs("\nNo complete positive-work candidate sample supports a numeric capacity coefficient.\n", output);
    }
    fputs("\nRebuild sensitivity: if a failed entry invalidates `d` later entries, budget the failed entry's consumed work **plus** the measured work discarded by those `d` entries and the replacement validations. `0 <= d <= K-1` bounds concurrent downstream exposure; do not multiply observed successful work by an invented failure rate. Keep integration concurrency bounded, choose shard fan-out jointly with account headroom and feedback latency, and admit optional preparation only within the residual budget using existing controls. This report changes no validation, cancellation, coalescing or coverage policy.\n", output);
}

static void accounting_runs_report(Accounting* accounting, FILE* output)
{
    fputs("\n## Run receipts and critical-path indicators\n\n", output);
    fputs("The last completing job and the longest observed job help locate bottlenecks; neither proves the dependency DAG's critical path. `Any runner` is the union of observed allocated job intervals across attempts, so it does not double-count concurrent jobs. Where every interval is known, wall time = any-runner occupation + outside-execution time; the latter includes admission, dependency waits and retry gaps and is not all runner queueing.\n\n", output);
    fputs("| Run | Cohort | Outcome / fate | Complete resources | Available runner-min | Feedback (s) | Completion proxy (s) | Any runner (s) | Outside execution (s) | Last completing job | Longest observed job |\n| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |\n", output);
    for (size_t i = 0; i < accounting->logical_count; i += 1)
    {
        const AccountingLogical* logical = &accounting->logical[i];
        const AccountingRun* latest = &accounting->runs[logical->latest];
        fprintf(output, "| [%llu](https://github.com/buster14a/buster/actions/runs/%llu) | %s | %s / %s | %s | %.3f | ", (unsigned long long)logical->id, (unsigned long long)logical->id, accounting_cohorts[latest->cohort], accounting_outcomes[latest->outcome], accounting_fates[latest->fate], logical->complete ? "yes" : "partial/unknown", (double)accounting_resources(logical) / 60);
        if (logical->feedback != BUSTER_ACCOUNTING_UNKNOWN)
        {
            fprintf(output, "%llu", (unsigned long long)logical->feedback);
        }
        else
        {
            fputs("unknown", output);
        }
        fputs(" | ", output);
        if (logical->completion != BUSTER_ACCOUNTING_UNKNOWN)
        {
            fprintf(output, "%llu", (unsigned long long)logical->completion);
        }
        else
        {
            fputs("unknown", output);
        }
        if (logical->wall_complete)
        {
            fprintf(output, " | %llu | %llu", (unsigned long long)logical->union_seconds, (unsigned long long)(logical->completion - logical->union_seconds));
        }
        else
        {
            fputs(" | unknown | unknown", output);
        }
        const size_t indicators[] = {
            logical->tail,
            logical->longest,
        };
        for (size_t j = 0; j < 2; j += 1)
        {
            fputs(" | ", output);
            if (indicators[j] != SIZE_MAX)
            {
                const AccountingJob* job = &accounting->jobs[indicators[j]];
                fprintf(output, "[%llu](https://github.com/buster14a/buster/actions/runs/%llu/job/%llu) ", (unsigned long long)job->id, (unsigned long long)logical->id, (unsigned long long)job->id);
                accounting_text(output, job->name);
                if (j == 1)
                {
                    fprintf(output, " (%llu s%s)", (unsigned long long)job->execution, job->exact ? "" : "+");
                }
            }
            else
            {
                fputs("unknown", output);
            }
        }
        fputs(" |\n", output);
    }
}

static void accounting_render(Accounting* accounting, FILE* output)
{
    fputs("# CI phase and capacity accounting\n\n", output);
    fprintf(output, "Snapshot epoch: %llu. Observed %zu workflow runs, %zu attempts and %zu jobs. All outcomes and available attempts are retained.\n", (unsigned long long)accounting->snapshot, accounting->logical_count, accounting->run_count, accounting->job_count);
    if (accounting->split != BUSTER_ACCOUNTING_UNKNOWN)
    {
        fprintf(output, "\nBefore/after split epoch: %llu (run creation, not job completion). Cohorts are reported separately.\n", (unsigned long long)accounting->split);
    }
    else
    {
        fputs("\nNo before/after split was supplied; the `all` cohort is a baseline, not an improvement claim.\n", output);
    }
    if (accounting->logical_count == 0)
    {
        fputs("\nNo run observations: all latency, demand and capacity conclusions are unknown.\n", output);
    }
    accounting_latency_report(accounting, output);
    accounting_phase_report(accounting, output);
    accounting_class_report(accounting, output);
    accounting_capacity_report(accounting, output);
    accounting_runs_report(accounting, output);
    fputs("\n## Collection limits and existing backlog reporting\n\n", output);
    fputs("The collector's raw API responses and normalized facts preserve the sampled run links, attempts, cohorts and incomplete reads. A missing/expired/unreadable page or log is unknown, never a successful zero. The report reader refuses malformed, duplicate or over-capacity facts instead of silently truncating.\n\n", output);
    for (size_t i = 0; i < accounting->note_count; i += 1)
    {
        fputs("- ", output);
        accounting_text(output, accounting->notes[i].source);
        fputs(": ", output);
        accounting_text(output, accounting->notes[i].text);
        fputc('\n', output);
    }
    fputs("\nMain backlog age and coverage remain owned by [#1439](https://github.com/buster14a/buster/issues/1439) and the [existing nightly ledger reports](https://github.com/buster14a/buster-performance/tree/nightly-ledger/nightly/reports). This is not another nightly ledger. Main pushes currently run no checks; historical main-push observations remain a separate cohort.\n", output);
}

static int accounting_report(FILE* input, FILE* output)
{
    Accounting* accounting = accounting_create();
    bool valid = accounting_allocated(accounting);
    char line[BUSTER_ACCOUNTING_LINE];
    size_t rows = 0;
    while (valid && fgets(line, sizeof(line), input) != NULL)
    {
        rows += 1;
        valid = accounting_add(accounting, line);
    }
    valid = valid && !ferror(input);
    if (valid)
    {
        valid = accounting_finish(accounting);
    }
    if (valid)
    {
        accounting_render(accounting, output);
        valid = fflush(output) == 0 && !ferror(output);
    }
    else
    {
        fprintf(stderr, "timings: invalid, duplicate, out-of-order or over-capacity accounting facts at/after row %zu\n", rows);
    }
    accounting_destroy(accounting);
    return valid ? 0 : 1;
}

