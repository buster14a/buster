// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Whether a failed run of the hosted correctness workflow is re-run once (#2276): the jobs API's record of the run's
// first attempt in (with the annotations of its cancelled jobs), a decision and each stopped job's reason out. A run is
// re-run (rerun-failed-jobs) only when every job that stopped did so in setup, before any test ran, so that green keeps
// meaning the tests passed. A job stops at its first step that didn't pass: one that failed, or one that was cancelled
// or left running (the job's timeout, a lost runner, fail-fast). That step must be a setup step of the allowlist below
// (rerun_steps: GitHub's own "Set up job", the checkouts, the hosted tools, setup-hosted.sh, setup-baseline-runtime.sh
// and the fetches), or a step that fails only because a setup failure stopped another job (its row's compiler build,
// its row's draws, the required aggregate's needs). The jobs API reports a job that its timeout stopped exactly as one
// that fail-fast cancelled, a cancelled job whose step was cancelled; only the job's annotations tell them apart. A job
// that fail-fast cancelled, by its annotation, stopped nothing of its own, wherever it was. Everything else keeps the
// run as it is: a test step, the build, a timeout outside setup, a cancellation without fail-fast's annotation, a
// self-hosted job, a step this list doesn't name.
//
// tools/ci/rerun.sh is the API glue (fetching the jobs, the request, the notes), .github/workflows/rerun.yml the
// trigger, and docs/commands/ci.md ("Setup failures re-run once") the rule. --self-test --root DIR also checks the
// allowlist against the workflow files: every name it holds is a step of the workflows the trigger watches or of those
// they call, and the trigger watches exactly the workflows that call hosted-correctness.yml. tests/ci/rerun.sh runs it
// on runs recorded from the API.
//
// Layout: rerun_steps (the allowlist); rerun_json_* and rerun_read (the API's JSON, read without recursion: the jobs'
// pages, one document each, then a document {"job_id": ID, "message": TEXT} per annotation); rerun_classify (the rule);
// rerun_print_report and rerun_print_markdown (its outputs); rerun_policy (the workflow files); rerun_cases and
// rerun_test (the self-test); main. Administrative tooling, outside the compiler's hot paths.

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_RERUN_INPUT_LIMIT ((size_t)64 << 20)
#define BUSTER_RERUN_JOB_LIMIT 4096u
#define BUSTER_RERUN_STEP_LIMIT 262144u
#define BUSTER_RERUN_DEPTH_LIMIT 64u
#define BUSTER_RERUN_PATH_LIMIT 4096u
#define BUSTER_RERUN_ANNOTATION_LIMIT 65536u
// The annotations GitHub gives a job that fail-fast cancelled, and one that its timeout-minutes stopped.
#define BUSTER_RERUN_FAIL_FAST "The strategy configuration was canceled because"
#define BUSTER_RERUN_TIMEOUT "exceeded the maximum execution time"
// A reusable workflow's job is named `CALLER / JOB`: the row is the caller's part, `Tests on Ubuntu 26.04 (ARCH, ...)`.
#define BUSTER_RERUN_ROW_SEPARATOR " / "
#define BUSTER_RERUN_ROW_BUILD "Build the compiler once for the row's shards"
#define BUSTER_RERUN_TEST_STEP "Run correctness tests within the hosted CPU budget"
#define BUSTER_RERUN_ROW_WORKFLOW "hosted-correctness.yml"
#define BUSTER_RERUN_WORKFLOW "rerun.yml"

typedef enum StepKind StepKind;
enum StepKind
{
    // Sets the job up and tests nothing: a failure or an interruption here is the setup's.
    STEP_SETUP,
    // "Require this row's compiler product": fails a shard whose row's build didn't publish one.
    STEP_PRODUCT,
    // The vector row's verdict: fails when an item has no passing execution, as when its candidates stopped.
    STEP_ROW,
    // The required aggregate: fails when a job it needs didn't pass.
    STEP_RUN,
    // Anything else: a test, the build, a check, a report.
    STEP_OTHER,
};

typedef struct SetupStep SetupStep;
struct SetupStep
{
    const char* name;
    StepKind kind;
    // GitHub's own step, which no workflow file names.
    bool runner;
};

// The allowlist, by the step names of the watched workflows (9700x.yml, and hosted-correctness.yml and
// prepare-hosted-tools.yml, which it calls). It applies to every GitHub-hosted job of the watched run.
static const SetupStep rerun_steps[] = {
    // The runner's provisioning: the job's environment and its actions' downloads.
    {
        .name = "Set up job",
        .kind = STEP_SETUP,
        .runner = true,
    },
    // The checkouts: the rows' and the fuzz jobs', and each other hosted job's.
    {
        .name = "Check out triggering commit",
        .kind = STEP_SETUP,
    },
    {
        .name = "Check out the triggering commit and its parents",
        .kind = STEP_SETUP,
    },
    {
        .name = "Check out the run's commit and its first parent",
        .kind = STEP_SETUP,
    },
    {
        .name = "Check out the exact caller revision",
        .kind = STEP_SETUP,
    },
    {
        .name = "Check out the qualification policy",
        .kind = STEP_SETUP,
    },
    {
        .name = "Check out the pull request's history",
        .kind = STEP_SETUP,
    },
    {
        .name = "Check out reviewed first-party performance fixtures and acceptance harnesses",
        .kind = STEP_SETUP,
    },
    {
        .name = "Fetch the vector draw's script from this commit",
        .kind = STEP_SETUP,
    },
    // The hosted tools: .github/actions/hosted-tools (a cache, this run's artifacts, or the mirror), and
    // prepare-hosted-tools.yml's acquisitions for the run, the release inputs' and the offline inputs'.
    {
        .name = "Restore verified hosted release inputs",
        .kind = STEP_SETUP,
    },
    {
        .name = "Acquire missing inputs once and verify the complete set",
        .kind = STEP_SETUP,
    },
    {
        .name = "Acquire missing offline inputs once and check each as the jobs read it",
        .kind = STEP_SETUP,
    },
    // tools/ci/setup-hosted.sh: the rows' and the fuzz jobs'.
    {
        .name = "Install official Clang 23.1.4 and test tools",
        .kind = STEP_SETUP,
    },
    {
        .name = "Install official Clang, sanitizer and libFuzzer runtimes",
        .kind = STEP_SETUP,
    },
    // tools/ci/setup-baseline-runtime.sh and the fetches.
    {
        .name = "Fetch generic baseline startup objects and runtime for emulation",
        .kind = STEP_SETUP,
    },
    {
        .name = "Fetch and verify frozen corpus inputs",
        .kind = STEP_SETUP,
    },
    {
        .name = "Fetch c-testsuite at its pinned commit",
        .kind = STEP_SETUP,
    },
    {
        .name = "Fetch SQLite's sources at their pinned commit",
        .kind = STEP_SETUP,
    },
    // The steps that fail because a setup failure stopped another job.
    {
        .name = "Require this row's compiler product",
        .kind = STEP_PRODUCT,
    },
    {
        .name = "Require one passing qualified execution of each vector item",
        .kind = STEP_ROW,
    },
    {
        .name = "Require hosted and native correctness before successful qualification",
        .kind = STEP_RUN,
    },
};
#define BUSTER_RERUN_STEP_COUNT (sizeof(rerun_steps) / sizeof(rerun_steps[0]))

typedef struct Step Step;
struct Step
{
    const char* name;
    // NULL for null: a step that hasn't ended.
    const char* conclusion;
};

typedef struct Job Job;
struct Job
{
    uint64_t id;
    const char* name;
    const char* status;
    // NULL for null.
    const char* conclusion;
    const char* url;
    uint32_t first_step;
    uint32_t step_count;
    bool has_id;
    // A label `ubuntu-...`: GitHub's hosted image.
    bool ubuntu;
    bool self_hosted;
    // By its annotations: fail-fast cancelled it, or its timeout stopped it.
    bool fail_fast;
    bool timeout;
};

typedef struct Annotation Annotation;
struct Annotation
{
    uint64_t job;
    bool fail_fast;
    bool timeout;
};

typedef struct Run Run;
struct Run
{
    Job jobs[BUSTER_RERUN_JOB_LIMIT];
    uint32_t job_count;
    Step* steps;
    uint32_t step_count;
    // The total_count every page states.
    uint64_t total;
    uint32_t pages;
    Annotation* annotations;
    uint32_t annotation_count;
    // The decoded strings of the last read, which the jobs and steps point into.
    char* arena;
};

typedef enum Context Context;
enum Context
{
    CONTEXT_IGNORED,
    // A document: one page of the jobs, or one annotation.
    CONTEXT_PAGE,
    CONTEXT_JOBS,
    CONTEXT_JOB,
    CONTEXT_LABELS,
    CONTEXT_STEPS,
    CONTEXT_STEP,
};

typedef enum Key Key;
enum Key
{
    KEY_OTHER,
    KEY_JOBS,
    KEY_TOTAL_COUNT,
    KEY_ID,
    KEY_NAME,
    KEY_STATUS,
    KEY_CONCLUSION,
    KEY_HTML_URL,
    KEY_LABELS,
    KEY_STEPS,
    KEY_JOB_ID,
    KEY_MESSAGE,
    KEY_COUNT,
};

static const char* const rerun_keys[KEY_COUNT] = {
    [KEY_OTHER] = "",
    [KEY_JOBS] = "jobs",
    [KEY_TOTAL_COUNT] = "total_count",
    [KEY_ID] = "id",
    [KEY_NAME] = "name",
    [KEY_STATUS] = "status",
    [KEY_CONCLUSION] = "conclusion",
    [KEY_HTML_URL] = "html_url",
    [KEY_LABELS] = "labels",
    [KEY_STEPS] = "steps",
    [KEY_JOB_ID] = "job_id",
    [KEY_MESSAGE] = "message",
};

typedef enum Expect Expect;
enum Expect
{
    EXPECT_KEY_OR_CLOSE,
    EXPECT_KEY,
    EXPECT_COLON,
    EXPECT_VALUE,
    EXPECT_VALUE_OR_CLOSE,
    EXPECT_COMMA_OR_CLOSE,
};

typedef struct Frame Frame;
struct Frame
{
    Context context;
    Key key;
    Expect expect;
    bool object;
    // A document's: whether it had a jobs array and a total_count (a page), or a job_id and a message (an annotation).
    bool jobs;
    bool total;
    bool job_id;
    bool message;
};

typedef enum Value Value;
enum Value
{
    VALUE_STRING,
    VALUE_INTEGER,
    VALUE_NUMBER,
    VALUE_TRUE,
    VALUE_FALSE,
    VALUE_NULL,
};

typedef struct Parser Parser;
struct Parser
{
    const char* input;
    size_t length;
    size_t position;
    // Decoded strings, NUL-terminated: never longer than the input's quoted text, so the input's size bounds them.
    char* arena;
    size_t arena_used;
    Run* run;
    Frame frames[BUSTER_RERUN_DEPTH_LIMIT];
    uint32_t depth;
    // The annotation document being read.
    Annotation annotation;
    const char* error;
};

static bool rerun_fail(Parser* parser, const char* error)
{
    if (parser->error == NULL)
    {
        parser->error = error;
    }
    return false;
}

static void rerun_json_space(Parser* parser)
{
    while (parser->position < parser->length && (parser->input[parser->position] == ' ' || parser->input[parser->position] == '\t' || parser->input[parser->position] == '\n' || parser->input[parser->position] == '\r'))
    {
        parser->position += 1;
    }
}

static int rerun_hex(char c)
{
    int digit = -1;
    if (c >= '0' && c <= '9')
    {
        digit = c - '0';
    }
    else if (c >= 'a' && c <= 'f')
    {
        digit = c - 'a' + 10;
    }
    else if (c >= 'A' && c <= 'F')
    {
        digit = c - 'A' + 10;
    }
    return digit;
}

// Four hex digits at `position`, or -1.
static int32_t rerun_json_hex4(const Parser* parser, size_t position)
{
    int32_t value = -1;
    if (position + 4 <= parser->length)
    {
        value = 0;
        for (size_t i = 0; i < 4 && value >= 0; i += 1)
        {
            const int digit = rerun_hex(parser->input[position + i]);
            value = digit < 0 ? -1 : value * 16 + digit;
        }
    }
    return value;
}

static void rerun_utf8(Parser* parser, uint32_t code)
{
    char* out = parser->arena + parser->arena_used;
    size_t count = 0;
    if (code < 0x80)
    {
        out[count++] = (char)code;
    }
    else if (code < 0x800)
    {
        out[count++] = (char)(0xc0 | (code >> 6));
        out[count++] = (char)(0x80 | (code & 0x3f));
    }
    else if (code < 0x10000)
    {
        out[count++] = (char)(0xe0 | (code >> 12));
        out[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[count++] = (char)(0x80 | (code & 0x3f));
    }
    else
    {
        out[count++] = (char)(0xf0 | (code >> 18));
        out[count++] = (char)(0x80 | ((code >> 12) & 0x3f));
        out[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[count++] = (char)(0x80 | (code & 0x3f));
    }
    parser->arena_used += count;
}

// The string at the opening quote, decoded into the arena. A NUL (\u0000) or a lone surrogate is refused: the strings
// are names, and a C string couldn't hold the first.
static bool rerun_json_string(Parser* parser, const char** text)
{
    bool ok = true;
    bool closed = false;
    char* start = parser->arena + parser->arena_used;
    parser->position += 1;
    while (ok && !closed)
    {
        if (parser->position >= parser->length)
        {
            ok = rerun_fail(parser, "unterminated string");
        }
        else
        {
            const unsigned char c = (unsigned char)parser->input[parser->position];
            if (c == '"')
            {
                closed = true;
                parser->position += 1;
            }
            else if (c < 0x20)
            {
                ok = rerun_fail(parser, "unescaped control character in a string");
            }
            else if (c != '\\')
            {
                parser->arena[parser->arena_used] = (char)c;
                parser->arena_used += 1;
                parser->position += 1;
            }
            else if (parser->position + 1 >= parser->length)
            {
                ok = rerun_fail(parser, "unterminated escape");
            }
            else
            {
                const char escape = parser->input[parser->position + 1];
                static const char escapes[] = "\"\\/bfnrt";
                static const char decoded[] = "\"\\/\b\f\n\r\t";
                const char* found = escape != 0 ? strchr(escapes, escape) : NULL;
                if (found != NULL)
                {
                    parser->arena[parser->arena_used] = decoded[found - escapes];
                    parser->arena_used += 1;
                    parser->position += 2;
                }
                else if (escape != 'u')
                {
                    ok = rerun_fail(parser, "invalid escape");
                }
                else
                {
                    const int32_t high = rerun_json_hex4(parser, parser->position + 2);
                    uint32_t code = (uint32_t)high;
                    size_t consumed = 6;
                    if (high < 0)
                    {
                        ok = rerun_fail(parser, "invalid \\u escape");
                    }
                    else if (high >= 0xd800 && high < 0xdc00)
                    {
                        const bool paired = parser->position + 12 <= parser->length && parser->input[parser->position + 6] == '\\' && parser->input[parser->position + 7] == 'u';
                        const int32_t low = paired ? rerun_json_hex4(parser, parser->position + 8) : -1;
                        if (low >= 0xdc00 && low < 0xe000)
                        {
                            code = 0x10000 + (((uint32_t)high - 0xd800) << 10) + ((uint32_t)low - 0xdc00);
                            consumed = 12;
                        }
                        else
                        {
                            ok = rerun_fail(parser, "unpaired surrogate");
                        }
                    }
                    else if (high >= 0xdc00 && high < 0xe000)
                    {
                        ok = rerun_fail(parser, "unpaired surrogate");
                    }
                    else if (high == 0)
                    {
                        ok = rerun_fail(parser, "NUL in a string");
                    }
                    if (ok)
                    {
                        rerun_utf8(parser, code);
                        parser->position += consumed;
                    }
                }
            }
        }
    }
    if (ok)
    {
        parser->arena[parser->arena_used] = 0;
        parser->arena_used += 1;
        *text = start;
    }
    return ok;
}

// A number by JSON's grammar. `integer` holds it when it is a non-negative integer that fits.
static bool rerun_json_number(Parser* parser, Value* kind, uint64_t* integer)
{
    const char* input = parser->input;
    const size_t length = parser->length;
    size_t position = parser->position;
    bool negative = false;
    bool fits = true;
    bool fraction = false;
    uint64_t value = 0;
    bool ok = true;
    if (input[position] == '-')
    {
        negative = true;
        position += 1;
    }
    if (position < length && input[position] == '0')
    {
        position += 1;
    }
    else if (position < length && input[position] >= '1' && input[position] <= '9')
    {
        while (position < length && input[position] >= '0' && input[position] <= '9')
        {
            const uint64_t digit = (uint64_t)(input[position] - '0');
            fits = fits && value <= (UINT64_MAX - digit) / 10;
            value = fits ? value * 10 + digit : value;
            position += 1;
        }
    }
    else
    {
        ok = rerun_fail(parser, "invalid number");
    }
    if (ok && position < length && input[position] == '.')
    {
        fraction = true;
        position += 1;
        const size_t digits = position;
        while (position < length && input[position] >= '0' && input[position] <= '9')
        {
            position += 1;
        }
        ok = position > digits || rerun_fail(parser, "invalid number");
    }
    if (ok && position < length && (input[position] == 'e' || input[position] == 'E'))
    {
        fraction = true;
        position += 1;
        if (position < length && (input[position] == '+' || input[position] == '-'))
        {
            position += 1;
        }
        const size_t digits = position;
        while (position < length && input[position] >= '0' && input[position] <= '9')
        {
            position += 1;
        }
        ok = position > digits || rerun_fail(parser, "invalid number");
    }
    parser->position = position;
    *kind = !negative && !fraction && fits ? VALUE_INTEGER : VALUE_NUMBER;
    *integer = value;
    return ok;
}

static bool rerun_json_literal(Parser* parser, const char* word)
{
    const size_t length = strlen(word);
    bool ok = parser->position + length <= parser->length && memcmp(parser->input + parser->position, word, length) == 0;
    if (ok)
    {
        parser->position += length;
    }
    else
    {
        rerun_fail(parser, "invalid literal");
    }
    return ok;
}

static Key rerun_json_key(const char* text)
{
    Key key = KEY_OTHER;
    for (uint32_t i = KEY_OTHER + 1; i < KEY_COUNT; i += 1)
    {
        if (strcmp(text, rerun_keys[i]) == 0)
        {
            key = (Key)i;
        }
    }
    return key;
}

// The context of a container opened as `key`'s value (or as an element) inside `parent`.
static Context rerun_json_child(uint32_t depth, Context parent, Key key, bool object)
{
    Context context = CONTEXT_IGNORED;
    if (depth == 0 && object)
    {
        context = CONTEXT_PAGE;
    }
    else if (parent == CONTEXT_PAGE && key == KEY_JOBS && !object)
    {
        context = CONTEXT_JOBS;
    }
    else if (parent == CONTEXT_JOBS && object)
    {
        context = CONTEXT_JOB;
    }
    else if (parent == CONTEXT_JOB && key == KEY_LABELS && !object)
    {
        context = CONTEXT_LABELS;
    }
    else if (parent == CONTEXT_JOB && key == KEY_STEPS && !object)
    {
        context = CONTEXT_STEPS;
    }
    else if (parent == CONTEXT_STEPS && object)
    {
        context = CONTEXT_STEP;
    }
    return context;
}

// A scalar's place in the run: `parent` is its container's context and `key` its key there (KEY_OTHER in an array).
static bool rerun_json_scalar(Parser* parser, Context parent, Key key, Value kind, const char* text, uint64_t integer)
{
    Run* run = parser->run;
    Job* job = run->job_count != 0 ? &run->jobs[run->job_count - 1] : NULL;
    const bool string = kind == VALUE_STRING;
    const bool nullable = string || kind == VALUE_NULL;
    bool ok = true;
    if (parent == CONTEXT_PAGE && key == KEY_TOTAL_COUNT)
    {
        ok = kind == VALUE_INTEGER || rerun_fail(parser, "total_count isn't a count");
        Frame* page = &parser->frames[parser->depth - 1];
        page->total = ok;
        ok = ok && (run->pages == 0 || run->total == integer || rerun_fail(parser, "the pages' total_count differ"));
        run->total = integer;
    }
    else if (parent == CONTEXT_PAGE && key == KEY_JOB_ID)
    {
        ok = kind == VALUE_INTEGER || rerun_fail(parser, "an annotation's job_id isn't an integer");
        parser->frames[parser->depth - 1].job_id = ok;
        parser->annotation.job = integer;
    }
    else if (parent == CONTEXT_PAGE && key == KEY_MESSAGE)
    {
        ok = string || rerun_fail(parser, "an annotation's message isn't a string");
        parser->frames[parser->depth - 1].message = ok;
        parser->annotation.fail_fast = ok && strncmp(text, BUSTER_RERUN_FAIL_FAST, strlen(BUSTER_RERUN_FAIL_FAST)) == 0;
        parser->annotation.timeout = ok && strstr(text, BUSTER_RERUN_TIMEOUT) != NULL;
    }
    else if (parent == CONTEXT_JOBS || parent == CONTEXT_STEPS)
    {
        ok = rerun_fail(parser, "a job or a step isn't an object");
    }
    else if (parent == CONTEXT_LABELS)
    {
        ok = string || rerun_fail(parser, "a label isn't a string");
        job->ubuntu = job->ubuntu || (ok && strncmp(text, "ubuntu-", 7) == 0);
        job->self_hosted = job->self_hosted || (ok && strcmp(text, "self-hosted") == 0);
    }
    else if (parent == CONTEXT_JOB && key == KEY_ID)
    {
        ok = kind == VALUE_INTEGER || rerun_fail(parser, "a job's id isn't an integer");
        job->id = integer;
        job->has_id = ok;
    }
    else if (parent == CONTEXT_JOB && (key == KEY_NAME || key == KEY_HTML_URL))
    {
        ok = string || rerun_fail(parser, "a job's name or URL isn't a string");
        const char** field = key == KEY_NAME ? &job->name : &job->url;
        *field = text;
    }
    else if (parent == CONTEXT_JOB && (key == KEY_STATUS || key == KEY_CONCLUSION))
    {
        ok = nullable || rerun_fail(parser, "a job's status or conclusion isn't a string");
        const char** field = key == KEY_STATUS ? &job->status : &job->conclusion;
        *field = string ? text : NULL;
    }
    else if (parent == CONTEXT_STEP && (key == KEY_NAME || key == KEY_CONCLUSION))
    {
        Step* step = &run->steps[run->step_count - 1];
        ok = (key == KEY_NAME ? string : nullable) || rerun_fail(parser, "a step's name or conclusion isn't a string");
        const char** field = key == KEY_NAME ? &step->name : &step->conclusion;
        *field = string ? text : NULL;
    }
    return ok;
}

static bool rerun_json_open(Parser* parser, bool object)
{
    Run* run = parser->run;
    const uint32_t depth = parser->depth;
    const Frame* parent = depth != 0 ? &parser->frames[depth - 1] : NULL;
    const Context context = rerun_json_child(depth, parent != NULL ? parent->context : CONTEXT_IGNORED, parent != NULL && parent->object ? parent->key : KEY_OTHER, object);
    bool ok = depth < BUSTER_RERUN_DEPTH_LIMIT || rerun_fail(parser, "nested too deeply");
    ok = ok && (depth != 0 || object || rerun_fail(parser, "a page isn't an object"));
    if (ok && context == CONTEXT_PAGE)
    {
        memset(&parser->annotation, 0, sizeof(parser->annotation));
    }
    else if (ok && context == CONTEXT_JOBS)
    {
        parser->frames[depth - 1].jobs = true;
    }
    else if (ok && context == CONTEXT_JOB)
    {
        ok = run->job_count < BUSTER_RERUN_JOB_LIMIT || rerun_fail(parser, "too many jobs");
        if (ok)
        {
            Job* job = &run->jobs[run->job_count];
            memset(job, 0, sizeof(*job));
            run->job_count += 1;
        }
    }
    else if (ok && context == CONTEXT_STEPS)
    {
        Job* job = &run->jobs[run->job_count - 1];
        if (job->step_count == 0)
        {
            job->first_step = run->step_count;
        }
    }
    else if (ok && context == CONTEXT_STEP)
    {
        Job* job = &run->jobs[run->job_count - 1];
        ok = run->step_count < BUSTER_RERUN_STEP_LIMIT || rerun_fail(parser, "too many steps");
        // A job's steps are contiguous: nothing else adds one between its steps arrays.
        ok = ok && (job->first_step + job->step_count == run->step_count || rerun_fail(parser, "a job's steps aren't contiguous"));
        if (ok)
        {
            Step* step = &run->steps[run->step_count];
            step->name = NULL;
            step->conclusion = NULL;
            run->step_count += 1;
            job->step_count += 1;
        }
    }
    else if (ok && parent != NULL && (parent->context == CONTEXT_LABELS || (parent->context == CONTEXT_JOBS && !object) || (parent->context == CONTEXT_STEPS && !object)))
    {
        ok = rerun_fail(parser, "a job, a step or a label has the wrong type");
    }
    if (ok)
    {
        Frame* frame = &parser->frames[depth];
        memset(frame, 0, sizeof(*frame));
        frame->context = context;
        frame->key = KEY_OTHER;
        frame->expect = object ? EXPECT_KEY_OR_CLOSE : EXPECT_VALUE_OR_CLOSE;
        frame->object = object;
        parser->depth = depth + 1;
        parser->position += 1;
    }
    return ok;
}

static bool rerun_json_close(Parser* parser)
{
    Run* run = parser->run;
    const Frame* frame = &parser->frames[parser->depth - 1];
    bool ok = true;
    if (frame->context == CONTEXT_PAGE && (frame->jobs || frame->total))
    {
        ok = (frame->jobs && frame->total && !frame->job_id && !frame->message) || rerun_fail(parser, "a page lacks its jobs array or its total_count, or is an annotation too");
        run->pages += 1;
    }
    else if (frame->context == CONTEXT_PAGE)
    {
        ok = (frame->job_id && frame->message) || rerun_fail(parser, "a document is neither a page of jobs nor an annotation");
        ok = ok && (run->annotation_count < BUSTER_RERUN_ANNOTATION_LIMIT || rerun_fail(parser, "too many annotations"));
        if (ok)
        {
            run->annotations[run->annotation_count] = parser->annotation;
            run->annotation_count += 1;
        }
    }
    else if (frame->context == CONTEXT_JOB)
    {
        const Job* job = &run->jobs[run->job_count - 1];
        ok = (job->has_id && job->name != NULL && job->status != NULL) || rerun_fail(parser, "a job lacks its id, name or status");
    }
    else if (frame->context == CONTEXT_STEP)
    {
        ok = run->steps[run->step_count - 1].name != NULL || rerun_fail(parser, "a step lacks its name");
    }
    parser->depth -= 1;
    parser->position += 1;
    if (parser->depth != 0)
    {
        parser->frames[parser->depth - 1].expect = EXPECT_COMMA_OR_CLOSE;
    }
    return ok;
}

// A value at the parser's position: a container opens a frame, a scalar is stored where it belongs.
static bool rerun_json_value(Parser* parser)
{
    const uint32_t depth = parser->depth;
    const Frame* parent = depth != 0 ? &parser->frames[depth - 1] : NULL;
    const Context context = parent != NULL ? parent->context : CONTEXT_IGNORED;
    const Key key = parent != NULL && parent->object ? parent->key : KEY_OTHER;
    const char c = parser->position < parser->length ? parser->input[parser->position] : 0;
    bool ok = true;
    if (c == '{' || c == '[')
    {
        ok = rerun_json_open(parser, c == '{');
    }
    else
    {
        Value kind = VALUE_NULL;
        const char* text = NULL;
        uint64_t integer = 0;
        ok = depth != 0 || rerun_fail(parser, "a page isn't an object");
        if (ok && c == '"')
        {
            kind = VALUE_STRING;
            ok = rerun_json_string(parser, &text);
        }
        else if (ok && (c == '-' || (c >= '0' && c <= '9')))
        {
            ok = rerun_json_number(parser, &kind, &integer);
        }
        else if (ok && c == 't')
        {
            kind = VALUE_TRUE;
            ok = rerun_json_literal(parser, "true");
        }
        else if (ok && c == 'f')
        {
            kind = VALUE_FALSE;
            ok = rerun_json_literal(parser, "false");
        }
        else if (ok && c == 'n')
        {
            kind = VALUE_NULL;
            ok = rerun_json_literal(parser, "null");
        }
        else if (ok)
        {
            ok = rerun_fail(parser, "expected a value");
        }
        ok = ok && rerun_json_scalar(parser, context, key, kind, text, integer);
        if (ok)
        {
            parser->frames[depth - 1].expect = EXPECT_COMMA_OR_CLOSE;
        }
    }
    return ok;
}

// The pages in `input`, one JSON document after another, into `run`.
static bool rerun_json_parse(Parser* parser)
{
    bool ok = true;
    bool done = false;
    while (ok && !done)
    {
        rerun_json_space(parser);
        const bool end = parser->position >= parser->length;
        const char c = end ? 0 : parser->input[parser->position];
        if (parser->depth == 0)
        {
            done = end;
            ok = done || rerun_json_value(parser);
        }
        else if (end)
        {
            ok = rerun_fail(parser, "unexpected end of input");
        }
        else
        {
            Frame* frame = &parser->frames[parser->depth - 1];
            switch (frame->expect)
            {
                case EXPECT_KEY_OR_CLOSE:
                case EXPECT_KEY:
                {
                    if (c == '}' && frame->expect == EXPECT_KEY_OR_CLOSE)
                    {
                        ok = rerun_json_close(parser);
                    }
                    else if (c == '"')
                    {
                        const char* text = NULL;
                        ok = rerun_json_string(parser, &text);
                        frame->key = ok ? rerun_json_key(text) : KEY_OTHER;
                        frame->expect = EXPECT_COLON;
                    }
                    else
                    {
                        ok = rerun_fail(parser, "expected a key");
                    }
                } break;
                case EXPECT_COLON:
                {
                    ok = c == ':' || rerun_fail(parser, "expected ':'");
                    parser->position += 1;
                    frame->expect = EXPECT_VALUE;
                } break;
                case EXPECT_VALUE:
                {
                    ok = rerun_json_value(parser);
                } break;
                case EXPECT_VALUE_OR_CLOSE:
                {
                    ok = c == ']' ? rerun_json_close(parser) : rerun_json_value(parser);
                } break;
                case EXPECT_COMMA_OR_CLOSE:
                {
                    if (c == ',')
                    {
                        parser->position += 1;
                        frame->expect = frame->object ? EXPECT_KEY : EXPECT_VALUE;
                    }
                    else if (c == (frame->object ? '}' : ']'))
                    {
                        ok = rerun_json_close(parser);
                    }
                    else
                    {
                        ok = rerun_fail(parser, "expected ',' or a closing bracket");
                    }
                } break;
            }
        }
    }
    return ok;
}

// The run's pages from `input`, checked whole: at least one page, every job once, as many as total_count says.
static bool rerun_read(Run* run, const char* input, size_t length, const char** error)
{
    free(run->arena);
    run->arena = malloc(length + 1);
    Parser parser = {
        .input = input,
        .length = length,
        .arena = run->arena,
        .run = run,
    };
    run->job_count = 0;
    run->step_count = 0;
    run->total = 0;
    run->pages = 0;
    run->annotation_count = 0;
    bool ok = parser.arena != NULL || rerun_fail(&parser, "out of memory");
    ok = ok && rerun_json_parse(&parser);
    ok = ok && (run->pages != 0 || rerun_fail(&parser, "no page"));
    ok = ok && (run->total == run->job_count || rerun_fail(&parser, "the pages hold a different number of jobs than total_count"));
    for (uint32_t i = 0; ok && i < run->job_count; i += 1)
    {
        for (uint32_t j = i + 1; ok && j < run->job_count; j += 1)
        {
            ok = run->jobs[i].id != run->jobs[j].id || rerun_fail(&parser, "a job appears twice");
        }
    }
    // Each annotation marks its job, which the pages must hold.
    for (uint32_t a = 0; ok && a < run->annotation_count; a += 1)
    {
        const Annotation* annotation = &run->annotations[a];
        bool found = false;
        for (uint32_t i = 0; i < run->job_count; i += 1)
        {
            Job* job = &run->jobs[i];
            const bool match = job->id == annotation->job;
            job->fail_fast = job->fail_fast || (match && annotation->fail_fast);
            job->timeout = job->timeout || (match && annotation->timeout);
            found = found || match;
        }
        ok = found || rerun_fail(&parser, "an annotation names a job the pages don't hold");
    }
    *error = parser.error;
    return ok;
}

typedef enum Verdict Verdict;
enum Verdict
{
    // Passed or skipped: it didn't stop.
    VERDICT_NONE,
    // Stopped in setup, before any test ran.
    VERDICT_SETUP,
    // Stopped only because a setup failure stopped another job.
    VERDICT_FOLLOWS,
    // Cancelled with nothing of its own stopped: by fail-fast, or after its steps passed.
    VERDICT_CANCELLED,
    // Anything else: keeps the run as it is.
    VERDICT_KEEP,
    VERDICT_COUNT,
};

static const char* const rerun_verdicts[VERDICT_COUNT] = {
    [VERDICT_NONE] = "none",
    [VERDICT_SETUP] = "setup",
    [VERDICT_FOLLOWS] = "follows-setup",
    [VERDICT_CANCELLED] = "cancelled",
    [VERDICT_KEEP] = "keep",
};

// The verdicts as the Markdown names them.
static const char* const rerun_verdict_labels[VERDICT_COUNT] = {
    [VERDICT_NONE] = "Didn't stop",
    [VERDICT_SETUP] = "Setup",
    [VERDICT_FOLLOWS] = "Follows setup",
    [VERDICT_CANCELLED] = "Cancelled",
    [VERDICT_KEEP] = "Not setup",
};

typedef enum Reason Reason;
enum Reason
{
    REASON_NONE,
    REASON_SETUP_STEP,
    REASON_BEFORE_STEPS,
    REASON_ROW_BUILD,
    REASON_ROW_SETUP,
    REASON_RUN_SETUP,
    REASON_FAIL_FAST,
    REASON_NOT_STARTED,
    REASON_STEPS_PASSED,
    REASON_OUTSIDE_SETUP,
    REASON_TIMEOUT,
    REASON_INTERRUPTED,
    REASON_ROW_BUILD_NOT_SETUP,
    REASON_ROW_NOT_SETUP,
    REASON_RUN_NOT_SETUP,
    REASON_NOT_HOSTED,
    REASON_INCOMPLETE,
    REASON_NO_STEP_FAILED,
    REASON_COUNT,
};

typedef struct ReasonText ReasonText;
struct ReasonText
{
    const char* token;
    const char* text;
};

static const ReasonText rerun_reasons[REASON_COUNT] = {
    [REASON_NONE] = {
        .token = "-",
        .text = "didn't stop",
    },
    [REASON_SETUP_STEP] = {
        .token = "setup-step",
        .text = "a setup step",
    },
    [REASON_BEFORE_STEPS] = {
        .token = "before-steps",
        .text = "before any step ran",
    },
    [REASON_ROW_BUILD] = {
        .token = "row-build-setup",
        .text = "its row's compiler build stopped in setup",
    },
    [REASON_ROW_SETUP] = {
        .token = "row-setup",
        .text = "a job of its row stopped in setup",
    },
    [REASON_RUN_SETUP] = {
        .token = "run-setup",
        .text = "a job it needs stopped in setup",
    },
    [REASON_FAIL_FAST] = {
        .token = "fail-fast",
        .text = "fail-fast cancelled it after another job failed",
    },
    [REASON_NOT_STARTED] = {
        .token = "not-started",
        .text = "cancelled before any step ran",
    },
    [REASON_STEPS_PASSED] = {
        .token = "steps-passed",
        .text = "cancelled after every step it ran passed",
    },
    [REASON_OUTSIDE_SETUP] = {
        .token = "outside-setup",
        .text = "not a setup step",
    },
    [REASON_TIMEOUT] = {
        .token = "timeout",
        .text = "its timeout stopped it outside setup",
    },
    [REASON_INTERRUPTED] = {
        .token = "interrupted",
        .text = "interrupted outside setup, not by fail-fast",
    },
    [REASON_ROW_BUILD_NOT_SETUP] = {
        .token = "row-build-not-setup",
        .text = "its row's compiler build didn't stop in setup",
    },
    [REASON_ROW_NOT_SETUP] = {
        .token = "row-not-setup",
        .text = "no job of its row stopped in setup",
    },
    [REASON_RUN_NOT_SETUP] = {
        .token = "run-not-setup",
        .text = "no job of the run stopped in setup",
    },
    [REASON_NOT_HOSTED] = {
        .token = "not-hosted",
        .text = "not a GitHub-hosted job",
    },
    [REASON_INCOMPLETE] = {
        .token = "incomplete",
        .text = "the job didn't complete",
    },
    [REASON_NO_STEP_FAILED] = {
        .token = "no-step-failed",
        .text = "failed with no step failed",
    },
};

typedef struct Outcome Outcome;
struct Outcome
{
    Verdict verdict;
    Reason reason;
    // The step the job stopped at, an index into the run's steps, or -1.
    int64_t stop;
};

typedef struct Decision Decision;
struct Decision
{
    bool rerun;
    uint32_t stopped;
    uint32_t setup;
    uint32_t keep;
};

static bool rerun_is(const char* text, const char* value)
{
    return text != NULL && strcmp(text, value) == 0;
}

static StepKind rerun_step_kind(const char* name)
{
    StepKind kind = STEP_OTHER;
    for (size_t i = 0; i < BUSTER_RERUN_STEP_COUNT; i += 1)
    {
        if (rerun_is(name, rerun_steps[i].name))
        {
            kind = rerun_steps[i].kind;
        }
    }
    return kind;
}

// The length of the row part of a job's name, or 0 when it isn't a reusable workflow's job.
static size_t rerun_row_length(const char* name)
{
    const char* separator = strstr(name, BUSTER_RERUN_ROW_SEPARATOR);
    return separator != NULL ? (size_t)(separator - name) : 0;
}

static bool rerun_same_row(const char* name, size_t row, const char* other)
{
    return row != 0 && rerun_row_length(other) == row && memcmp(name, other, row) == 0;
}

// Each job's verdict, and the run's decision: a rerun when at least one job stopped in setup and every stopped job did
// so in setup, followed a setup failure, or was cancelled with nothing failed.
static Decision rerun_classify(const Run* run, Outcome* outcomes)
{
    Decision decision = { 0 };
    const uint32_t count = run->job_count;
    // The jobs' own stops, before the steps that follow other jobs' setup failures.
    for (uint32_t i = 0; i < count; i += 1)
    {
        const Job* job = &run->jobs[i];
        Outcome outcome = {
            .verdict = VERDICT_KEEP,
            .reason = REASON_OUTSIDE_SETUP,
            .stop = -1,
        };
        const bool completed = rerun_is(job->status, "completed");
        const bool passed = rerun_is(job->conclusion, "success") || rerun_is(job->conclusion, "skipped") || rerun_is(job->conclusion, "neutral");
        const bool cancelled = rerun_is(job->conclusion, "cancelled");
        if (completed && passed)
        {
            outcome.verdict = VERDICT_NONE;
            outcome.reason = REASON_NONE;
        }
        else if (!completed)
        {
            outcome.reason = REASON_INCOMPLETE;
        }
        else if (!job->ubuntu || job->self_hosted)
        {
            outcome.reason = REASON_NOT_HOSTED;
        }
        else
        {
            // The job's first step that didn't pass: failed, or cancelled or left running (null).
            for (uint32_t index = job->first_step; index < job->first_step + job->step_count && outcome.stop < 0; index += 1)
            {
                const Step* step = &run->steps[index];
                const bool step_passed = rerun_is(step->conclusion, "success") || rerun_is(step->conclusion, "skipped");
                outcome.stop = step_passed ? -1 : (int64_t)index;
            }
            const Step* stop = outcome.stop >= 0 ? &run->steps[outcome.stop] : NULL;
            const bool interrupted = stop != NULL && (stop->conclusion == NULL || rerun_is(stop->conclusion, "cancelled"));
            const StepKind kind = stop != NULL ? rerun_step_kind(stop->name) : STEP_OTHER;
            if (stop == NULL && job->step_count == 0)
            {
                // Nothing ran: its provisioning failed, or another job's failure cancelled it before it started.
                outcome.verdict = cancelled ? VERDICT_CANCELLED : VERDICT_SETUP;
                outcome.reason = cancelled ? REASON_NOT_STARTED : REASON_BEFORE_STEPS;
            }
            else if (stop == NULL)
            {
                // Nothing failed and nothing was interrupted: a cancellation hides no failure, a failure is unexplained.
                outcome.verdict = cancelled ? VERDICT_CANCELLED : VERDICT_KEEP;
                outcome.reason = cancelled ? REASON_STEPS_PASSED : REASON_NO_STEP_FAILED;
            }
            else if (kind == STEP_SETUP)
            {
                // Failed in setup, or stopped there by fail-fast, its timeout or a lost runner: whatever stopped it, no
                // test ran, so its annotations don't matter.
                outcome.verdict = VERDICT_SETUP;
                outcome.reason = REASON_SETUP_STEP;
            }
            else if (interrupted && cancelled && job->fail_fast && !job->timeout)
            {
                // Another job's failure stopped it, wherever it was: that failure decides.
                outcome.verdict = VERDICT_CANCELLED;
                outcome.reason = REASON_FAIL_FAST;
            }
            else if (interrupted)
            {
                outcome.reason = job->timeout ? REASON_TIMEOUT : REASON_INTERRUPTED;
            }
            else if (kind != STEP_OTHER)
            {
                outcome.verdict = VERDICT_FOLLOWS;
            }
        }
        outcomes[i] = outcome;
    }
    // The steps that fail because a setup failure stopped another job: its row's build, a job of its row, any job.
    for (uint32_t i = 0; i < count; i += 1)
    {
        Outcome* outcome = &outcomes[i];
        if (outcome->verdict == VERDICT_FOLLOWS)
        {
            const char* name = run->jobs[i].name;
            const size_t row = rerun_row_length(name);
            const StepKind kind = rerun_step_kind(run->steps[outcome->stop].name);
            bool found = false;
            for (uint32_t j = 0; j < count; j += 1)
            {
                const char* other = run->jobs[j].name;
                const bool setup = outcomes[j].verdict == VERDICT_SETUP;
                const bool build = rerun_same_row(name, row, other) && strcmp(other + row + strlen(BUSTER_RERUN_ROW_SEPARATOR), BUSTER_RERUN_ROW_BUILD) == 0;
                found = found || (setup && kind == STEP_PRODUCT && build) || (setup && kind == STEP_ROW && rerun_same_row(name, row, other)) || (setup && kind == STEP_RUN);
            }
            const Reason follows = kind == STEP_PRODUCT ? REASON_ROW_BUILD : kind == STEP_ROW ? REASON_ROW_SETUP : REASON_RUN_SETUP;
            const Reason unfollowed = kind == STEP_PRODUCT ? REASON_ROW_BUILD_NOT_SETUP : kind == STEP_ROW ? REASON_ROW_NOT_SETUP : REASON_RUN_NOT_SETUP;
            outcome->verdict = found ? VERDICT_FOLLOWS : VERDICT_KEEP;
            outcome->reason = found ? follows : unfollowed;
        }
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        decision.stopped += outcomes[i].verdict != VERDICT_NONE;
        decision.setup += outcomes[i].verdict == VERDICT_SETUP;
        decision.keep += outcomes[i].verdict == VERDICT_KEEP;
    }
    decision.rerun = decision.setup != 0 && decision.keep == 0;
    return decision;
}

// Whether job `index`'s verdict could change with its annotations: a cancelled job interrupted outside setup, which
// fail-fast's annotation excuses and a timeout's doesn't.
static bool rerun_interrupted(const Run* run, const Outcome* outcomes, uint32_t index)
{
    const Outcome* outcome = &outcomes[index];
    return rerun_is(run->jobs[index].conclusion, "cancelled") && outcome->verdict == VERDICT_KEEP && outcome->reason == REASON_INTERRUPTED;
}

// Whether job `index`'s annotations could change the run's decision, as classified without them: it is interrupted
// outside setup, a job stopped in setup, and no other job keeps the run. Annotations can only excuse such a job, so no
// other run needs any. tools/ci/rerun.sh fetches no others: every run shares the Actions token's hourly API quota,
// which the vector draws' calls exhausted on 2026-11-11.
static bool rerun_needs_annotations(const Run* run, const Outcome* outcomes, Decision decision, uint32_t index)
{
    uint32_t interrupted = 0;
    for (uint32_t i = 0; i < run->job_count; i += 1)
    {
        interrupted += rerun_interrupted(run, outcomes, i);
    }
    return decision.setup != 0 && decision.keep == interrupted && rerun_interrupted(run, outcomes, index);
}

// A name as one field of a line: tabs, newlines and other control characters become spaces.
static void rerun_print_field(FILE* out, const char* text)
{
    const char* value = text != NULL ? text : "-";
    for (size_t i = 0; value[i] != 0; i += 1)
    {
        const unsigned char c = (unsigned char)value[i];
        fputc(c < 0x20 || c == 0x7f ? ' ' : c, out);
    }
}

// The report: `decision rerun|keep`, the counts, then a line per stopped job: verdict, reason, conclusion, the step it
// stopped at (`-` when none), its name.
static void rerun_print_report(FILE* out, const Run* run, const Outcome* outcomes, Decision decision)
{
    fprintf(out, "decision\t%s\n", decision.rerun ? "rerun" : "keep");
    fprintf(out, "jobs\t%u\tstopped\t%u\tsetup\t%u\tkeep\t%u\n", run->job_count, decision.stopped, decision.setup, decision.keep);
    for (uint32_t i = 0; i < run->job_count; i += 1)
    {
        const Outcome* outcome = &outcomes[i];
        if (outcome->verdict != VERDICT_NONE)
        {
            const Job* job = &run->jobs[i];
            fprintf(out, "job\t%s\t%s\t", rerun_verdicts[outcome->verdict], rerun_reasons[outcome->reason].token);
            rerun_print_field(out, job->conclusion);
            fputc('\t', out);
            rerun_print_field(out, outcome->stop >= 0 ? run->steps[outcome->stop].name : NULL);
            fputc('\t', out);
            rerun_print_field(out, job->name);
            fputc('\n', out);
        }
    }
}

// A name in a code span, where Markdown and mentions don't apply: a backtick becomes an apostrophe, a control
// character a space.
static void rerun_print_code(FILE* out, const char* text)
{
    fputc('`', out);
    for (size_t i = 0; text[i] != 0; i += 1)
    {
        const unsigned char c = (unsigned char)text[i];
        fputc(c == '`' ? '\'' : c < 0x20 || c == 0x7f ? ' ' : c, out);
    }
    fputc('`', out);
}

// Whether a job's URL can be a link as it is: GitHub's, with no character Markdown would read.
static bool rerun_url(const char* url)
{
    bool safe = url != NULL && strncmp(url, "https://github.com/", 19) == 0;
    for (size_t i = 0; safe && url[i] != 0; i += 1)
    {
        const char c = url[i];
        safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '.' || c == ':' || c == '-' || c == '_';
    }
    return safe;
}

// The stopped jobs as a Markdown list, grouped by the step they stopped at and why, in the order they first appear.
static void rerun_print_markdown(FILE* out, const Run* run, const Outcome* outcomes)
{
    const uint32_t count = run->job_count;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const Outcome* first = &outcomes[i];
        const char* step = first->stop >= 0 ? run->steps[first->stop].name : NULL;
        bool seen = first->verdict == VERDICT_NONE;
        for (uint32_t j = 0; j < i && !seen; j += 1)
        {
            const char* other = outcomes[j].stop >= 0 ? run->steps[outcomes[j].stop].name : NULL;
            seen = outcomes[j].verdict != VERDICT_NONE && outcomes[j].reason == first->reason && (step == other || (step != NULL && other != NULL && strcmp(step, other) == 0));
        }
        if (!seen)
        {
            uint32_t members = 0;
            for (uint32_t j = i; j < count; j += 1)
            {
                const char* other = outcomes[j].stop >= 0 ? run->steps[outcomes[j].stop].name : NULL;
                members += outcomes[j].verdict != VERDICT_NONE && outcomes[j].reason == first->reason && (step == other || (step != NULL && other != NULL && strcmp(step, other) == 0));
            }
            fputs("- ", out);
            if (step != NULL)
            {
                rerun_print_code(out, step);
            }
            else
            {
                fputs("No step", out);
            }
            fprintf(out, " (%u %s). %s: %s.\n", members, members == 1 ? "job" : "jobs", rerun_verdict_labels[first->verdict], rerun_reasons[first->reason].text);
            for (uint32_t j = i; j < count; j += 1)
            {
                const char* other = outcomes[j].stop >= 0 ? run->steps[outcomes[j].stop].name : NULL;
                if (outcomes[j].verdict != VERDICT_NONE && outcomes[j].reason == first->reason && (step == other || (step != NULL && other != NULL && strcmp(step, other) == 0)))
                {
                    const Job* job = &run->jobs[j];
                    const bool link = rerun_url(job->url);
                    fputs(link ? "  - [" : "  - ", out);
                    rerun_print_code(out, job->name);
                    if (link)
                    {
                        fprintf(out, "](%s)", job->url);
                    }
                    fputs(" (", out);
                    rerun_print_field(out, job->conclusion);
                    fputs(")\n", out);
                }
            }
        }
    }
}

// A whole file, at most BUSTER_RERUN_INPUT_LIMIT bytes, NUL-terminated. NULL when it can't be read or is larger.
static char* rerun_read_file(const char* path, size_t* length)
{
    size_t capacity = (size_t)1 << 16;
    size_t used = 0;
    char* data = malloc(capacity);
    FILE* file = fopen(path, "rb");
    bool ok = data != NULL && file != NULL;
    bool done = false;
    while (ok && !done)
    {
        if (used + 1 == capacity)
        {
            capacity *= 2;
            char* grown = capacity <= BUSTER_RERUN_INPUT_LIMIT ? realloc(data, capacity) : NULL;
            ok = grown != NULL;
            data = ok ? grown : data;
        }
        const size_t got = ok ? fread(data + used, 1, capacity - used - 1, file) : 0;
        used += got;
        done = ok && got == 0;
        ok = ok && !ferror(file);
    }
    if (file != NULL)
    {
        fclose(file);
    }
    if (ok)
    {
        data[used] = 0;
        *length = used;
    }
    else
    {
        free(data);
        data = NULL;
    }
    return data;
}

// The text after `name: ` on a line of a workflow file, as a step, a job or the workflow names itself: the line's
// indentation and a sequence entry's `- ` go first, then quotes around the value.
static bool rerun_yaml_name(const char* line, size_t length, const char** value, size_t* value_length)
{
    size_t position = 0;
    while (position < length && line[position] == ' ')
    {
        position += 1;
    }
    if (position + 2 <= length && line[position] == '-' && line[position + 1] == ' ')
    {
        position += 2;
    }
    const bool found = position + 6 <= length && memcmp(line + position, "name: ", 6) == 0;
    if (found)
    {
        size_t start = position + 6;
        size_t end = length;
        while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\r'))
        {
            end -= 1;
        }
        if (end - start >= 2 && (line[start] == '\'' || line[start] == '"') && line[end - 1] == line[start])
        {
            start += 1;
            end -= 1;
        }
        *value = line + start;
        *value_length = end - start;
    }
    return found;
}

// Whether a workflow file names a step or a job `name`.
static bool rerun_yaml_has_name(const char* text, const char* name)
{
    const size_t name_length = strlen(name);
    bool found = false;
    const char* line = text;
    while (!found && *line != 0)
    {
        const char* end = strchr(line, '\n');
        const size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
        const char* value = NULL;
        size_t value_length = 0;
        found = rerun_yaml_name(line, length, &value, &value_length) && value_length == name_length && memcmp(value, name, name_length) == 0;
        line = end != NULL ? end + 1 : line + length;
    }
    return found;
}

// The workflow's own name: its first unindented `name:` line. Empty when it has none.
static void rerun_yaml_workflow_name(const char* text, char* name, size_t capacity)
{
    const char* line = text;
    bool found = false;
    name[0] = 0;
    while (!found && *line != 0)
    {
        const char* end = strchr(line, '\n');
        const size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
        const char* value = NULL;
        size_t value_length = 0;
        found = line[0] != ' ' && rerun_yaml_name(line, length, &value, &value_length) && value_length < capacity;
        if (found)
        {
            memcpy(name, value, value_length);
            name[value_length] = 0;
        }
        line = end != NULL ? end + 1 : line + length;
    }
}

typedef struct Policy Policy;
struct Policy
{
    uint32_t checks;
    uint32_t failures;
};

static void rerun_expect(Policy* policy, bool condition, const char* what)
{
    policy->checks += 1;
    if (!condition)
    {
        policy->failures += 1;
        fprintf(stderr, "rerun --self-test: %s\n", what);
    }
}

static char* rerun_read_workflow(const char* root, const char* file)
{
    char path[BUSTER_RERUN_PATH_LIMIT];
    size_t length = 0;
    const int written = snprintf(path, sizeof(path), "%s/.github/workflows/%s", root, file);
    return written > 0 && (size_t)written < sizeof(path) ? rerun_read_file(path, &length) : NULL;
}

// Whether the trigger's `workflows: [...]` list, after its `workflow_run:`, holds `name` quoted.
static bool rerun_watches(const char* trigger, const char* name)
{
    const char* on = strstr(trigger, "workflow_run:");
    const char* list = on != NULL ? strstr(on, "workflows: [") : NULL;
    const char* end = list != NULL ? strchr(list, ']') : NULL;
    const size_t length = strlen(name);
    bool found = false;
    for (const char* quote = list != NULL ? strchr(list, '"') : NULL; !found && quote != NULL && quote < end; quote = strchr(quote + 1, '"'))
    {
        found = strncmp(quote + 1, name, length) == 0 && quote[length + 1] == '"';
    }
    return found;
}

// The allowlist against the workflow files under `root`. The trigger (rerun.yml) watches exactly the workflows that
// call the row workflow, by name, which is what workflow_run matches, and re-runs only their failed first attempts.
// Every step the allowlist names is a step of the watched workflows or of the workflows they call, and the row's build
// job and test step keep the names the rule reads.
static void rerun_policy(Policy* policy, const char* root)
{
    static const char* const guards[] = {
        "types: [completed]",
        "github.event.workflow_run.conclusion == 'failure'",
        "github.event.workflow_run.run_attempt == 1",
        "github.event.workflow_run.head_repository.full_name == github.repository",
    };
    char* trigger = rerun_read_workflow(root, BUSTER_RERUN_WORKFLOW);
    rerun_expect(policy, trigger != NULL, "rerun.yml is readable");
    for (size_t i = 0; trigger != NULL && i < sizeof(guards) / sizeof(guards[0]); i += 1)
    {
        rerun_expect(policy, strstr(trigger, guards[i]) != NULL, "rerun.yml re-runs only a completed, failed first attempt of this repository's own runs");
    }
    // Every workflow file: the callers of the row workflow, and what they call.
    char directory[BUSTER_RERUN_PATH_LIMIT];
    snprintf(directory, sizeof(directory), "%s/.github/workflows", root);
    DIR* listing = opendir(directory);
    rerun_expect(policy, listing != NULL, "the workflows' directory is readable");
    char* steps = NULL;
    size_t steps_length = 0;
    uint32_t watched = 0;
    bool row_read = false;
    for (struct dirent* entry = listing != NULL ? readdir(listing) : NULL; trigger != NULL && entry != NULL; entry = readdir(listing))
    {
        const size_t length = strlen(entry->d_name);
        const bool yaml = length > 4 && strcmp(entry->d_name + length - 4, ".yml") == 0;
        char* text = yaml && strcmp(entry->d_name, BUSTER_RERUN_WORKFLOW) != 0 ? rerun_read_workflow(root, entry->d_name) : NULL;
        if (text != NULL)
        {
            char name[256];
            rerun_yaml_workflow_name(text, name, sizeof(name));
            const bool caller = strstr(text, "uses: ./.github/workflows/" BUSTER_RERUN_ROW_WORKFLOW) != NULL;
            const bool watches = name[0] != 0 && rerun_watches(trigger, name);
            if (caller != watches)
            {
                fprintf(stderr, "rerun --self-test: %s (%s)\n", entry->d_name, name);
            }
            rerun_expect(policy, caller == watches, "rerun.yml watches every workflow that calls the row workflow, by its name, and no other");
            watched += watches;
            // A watched workflow's steps, and those of the workflows it calls, are the ones the allowlist may name.
            const char* call = caller ? text : NULL;
            bool keep = caller;
            while (call != NULL)
            {
                call = strstr(call, "uses: ./.github/workflows/");
                if (call != NULL)
                {
                    call += strlen("uses: ./.github/workflows/");
                    size_t called_length = 0;
                    while (call[called_length] != 0 && call[called_length] != '\n' && call[called_length] != ' ' && call[called_length] != '\r')
                    {
                        called_length += 1;
                    }
                    char called[256];
                    const bool fits = called_length < sizeof(called);
                    if (fits)
                    {
                        memcpy(called, call, called_length);
                        called[called_length] = 0;
                    }
                    char* callee = fits ? rerun_read_workflow(root, called) : NULL;
                    rerun_expect(policy, callee != NULL, "a watched workflow's reusable workflows are readable");
                    row_read = row_read || (callee != NULL && strcmp(called, BUSTER_RERUN_ROW_WORKFLOW) == 0 && rerun_yaml_has_name(callee, BUSTER_RERUN_ROW_BUILD) && rerun_yaml_has_name(callee, BUSTER_RERUN_TEST_STEP));
                    const size_t callee_length = callee != NULL ? strlen(callee) : 0;
                    char* grown = callee != NULL ? realloc(steps, steps_length + callee_length + 2) : steps;
                    if (callee != NULL && grown != NULL)
                    {
                        steps = grown;
                        memcpy(steps + steps_length, callee, callee_length);
                        steps_length += callee_length;
                        steps[steps_length] = '\n';
                        steps_length += 1;
                        steps[steps_length] = 0;
                    }
                    rerun_expect(policy, callee == NULL || grown != NULL, "out of memory");
                    free(callee);
                }
            }
            const size_t text_length = keep ? strlen(text) : 0;
            char* grown = keep ? realloc(steps, steps_length + text_length + 2) : steps;
            if (keep && grown != NULL)
            {
                steps = grown;
                memcpy(steps + steps_length, text, text_length);
                steps_length += text_length;
                steps[steps_length] = '\n';
                steps_length += 1;
                steps[steps_length] = 0;
            }
            rerun_expect(policy, !keep || grown != NULL, "out of memory");
        }
        free(text);
    }
    if (listing != NULL)
    {
        closedir(listing);
    }
    rerun_expect(policy, watched != 0, "rerun.yml watches a workflow that calls the row workflow");
    // Each name the trigger watches is a workflow's.
    const char* on = trigger != NULL ? strstr(trigger, "workflow_run:") : NULL;
    const char* list = on != NULL ? strstr(on, "workflows: [") : NULL;
    const char* end = list != NULL ? strchr(list, ']') : NULL;
    uint32_t names = 0;
    for (const char* quote = list != NULL ? strchr(list, '"') : NULL; quote != NULL && quote < end; quote = strchr(quote + 1, '"'))
    {
        names += 1;
    }
    rerun_expect(policy, names == 2 * watched, "every workflow rerun.yml watches calls the row workflow");
    for (size_t i = 0; steps != NULL && i < BUSTER_RERUN_STEP_COUNT; i += 1)
    {
        const bool named = rerun_yaml_has_name(steps, rerun_steps[i].name);
        if (named == rerun_steps[i].runner)
        {
            fprintf(stderr, "rerun --self-test: allowlisted step: %s\n", rerun_steps[i].name);
        }
        rerun_expect(policy, named != rerun_steps[i].runner, "each allowlisted step is one of the watched workflows' (GitHub's own steps are none of theirs)");
    }
    rerun_expect(policy, row_read, "the row workflow's build job and test step keep their names");
    rerun_expect(policy, rerun_step_kind(BUSTER_RERUN_TEST_STEP) == STEP_OTHER, "the test step is no setup step");
    free(steps);
    free(trigger);
}

typedef struct Case Case;
struct Case
{
    const char* what;
    const char* json;
    // The decision, or NULL for input the reader must refuse.
    const char* decision;
    // The stopped jobs' verdicts and reasons, in the jobs' order, as `verdict/reason` separated by spaces.
    const char* verdicts;
};

// A hosted job (`ubuntu-26.04`) named NAME, concluded CONCLUSION, with the STEPS given as JSON objects.
#define BUSTER_RERUN_JOB(id, name, conclusion, steps) "{\"id\":" #id ",\"name\":\"" name "\",\"status\":\"completed\",\"conclusion\":" conclusion ",\"labels\":[\"ubuntu-26.04\"],\"steps\":[" steps "]}"
#define BUSTER_RERUN_STEP(name, conclusion) "{\"name\":\"" name "\",\"status\":\"completed\",\"conclusion\":" conclusion ",\"number\":1}"
#define BUSTER_RERUN_PAGE(total, jobs) "{\"total_count\":" #total ",\"jobs\":[" jobs "]}"
// One annotation of job ID, as tools/ci/rerun.sh writes it: MESSAGE is JSON string content.
#define BUSTER_RERUN_ANNOTATION(id, message) "{\"job_id\":" #id ",\"message\":\"" message "\"}\n"
#define BUSTER_RERUN_SETUP BUSTER_RERUN_STEP("Set up job", "\"success\"")
#define BUSTER_RERUN_ROW "Tests on Ubuntu 26.04 (x86-64, none, O3) / "
#define BUSTER_RERUN_OTHER_ROW "Tests on Ubuntu 26.04 (native, none, O3) / "

static const Case rerun_cases[] = {
    {
        .what = "a setup failure alone",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, "changes", "\"success\"", BUSTER_RERUN_SETUP) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_SETUP "," BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"") "," BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"skipped\""))),
        .decision = "rerun",
        .verdicts = "setup/setup-step",
    },
    {
        .what = "a test failure alone",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_SETUP "," BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"success\"") "," BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"failure\""))),
        .decision = "keep",
        .verdicts = "keep/outside-setup",
    },
    {
        .what = "a setup failure beside a test failure",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Fetch c-testsuite at its pinned commit", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"failure\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"failure\""))),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/outside-setup",
    },
    {
        .what = "a compile failure in the build step",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Build the external Clang pragma oracle, the clang server and the compiler", "\"failure\""))),
        .decision = "keep",
        .verdicts = "keep/outside-setup",
    },
    {
        .what = "a test step a lost runner left running",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "null")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"failure\"", BUSTER_RERUN_STEP("Set up job", "\"failure\""))),
        .decision = "keep",
        .verdicts = "keep/interrupted setup/setup-step",
    },
    {
        .what = "a setup step a lost runner left running",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_SETUP "," BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "null") "," BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "null"))),
        .decision = "rerun",
        .verdicts = "setup/setup-step",
    },
    {
        .what = "a job that failed before any step",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", "")),
        .decision = "rerun",
        .verdicts = "setup/before-steps",
    },
    {
        .what = "shards without their row's build, which stopped in setup",
        .json = BUSTER_RERUN_PAGE(3, BUSTER_RERUN_JOB(1, BUSTER_RERUN_ROW BUSTER_RERUN_ROW_BUILD, "\"failure\"", BUSTER_RERUN_STEP("Restore verified hosted release inputs", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Require this row's compiler product", "\"failure\"")) "," BUSTER_RERUN_JOB(3, BUSTER_RERUN_ROW "SQLite", "\"failure\"", BUSTER_RERUN_STEP("Require this row's compiler product", "\"failure\""))),
        .decision = "rerun",
        .verdicts = "setup/setup-step follows-setup/row-build-setup follows-setup/row-build-setup",
    },
    {
        .what = "shards without their row's build, which failed to compile",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_ROW BUSTER_RERUN_ROW_BUILD, "\"failure\"", BUSTER_RERUN_STEP("Build the external Clang pragma oracle, the clang server and the compiler", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Require this row's compiler product", "\"failure\""))),
        .decision = "keep",
        .verdicts = "keep/outside-setup keep/row-build-not-setup",
    },
    {
        .what = "shards without a build of another row's",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW BUSTER_RERUN_ROW_BUILD, "\"failure\"", BUSTER_RERUN_STEP("Set up job", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Require this row's compiler product", "\"failure\""))),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/row-build-not-setup",
    },
    {
        .what = "the vector verdict after a candidate's setup failure, and the aggregate",
        .json = BUSTER_RERUN_PAGE(3, BUSTER_RERUN_JOB(1, BUSTER_RERUN_ROW "Vector draw 1, candidate 2", "\"failure\"", BUSTER_RERUN_STEP("Probe this runner's CPU for the explicit vector target", "\"success\"") "," BUSTER_RERUN_STEP("Check out triggering commit", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_ROW "Vector row verdict", "\"failure\"", BUSTER_RERUN_STEP("Require one passing qualified execution of each vector item", "\"failure\"")) "," BUSTER_RERUN_JOB(3, "Build and execute on Ryzen 7 9700X", "\"failure\"", BUSTER_RERUN_STEP("Require hosted and native correctness before successful qualification", "\"failure\""))),
        .decision = "rerun",
        .verdicts = "setup/setup-step follows-setup/row-setup follows-setup/run-setup",
    },
    {
        .what = "an incomplete vector verdict and an aggregate without a setup failure",
        .json = BUSTER_RERUN_PAGE(3, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_ROW "Vector row verdict", "\"failure\"", BUSTER_RERUN_STEP("Require one passing qualified execution of each vector item", "\"failure\"")) "," BUSTER_RERUN_JOB(3, "Build and execute on Ryzen 7 9700X", "\"failure\"", BUSTER_RERUN_STEP("Require hosted and native correctness before successful qualification", "\"failure\""))),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/row-not-setup follows-setup/run-setup",
    },
    {
        .what = "the aggregate alone",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(3, "Build and execute on Ryzen 7 9700X", "\"failure\"", BUSTER_RERUN_STEP("Require hosted and native correctness before successful qualification", "\"failure\""))),
        .decision = "keep",
        .verdicts = "keep/run-not-setup",
    },
    {
        .what = "fail-fast cancellations in setup and in a draw, and one after its steps passed, beside a setup failure",
        .json = BUSTER_RERUN_PAGE(4, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"cancelled\"")) "," BUSTER_RERUN_JOB(3, BUSTER_RERUN_ROW "Vector draw 1, candidate 3", "\"cancelled\"", BUSTER_RERUN_STEP("Take the vector item the draw assigns, or end as a redraw", "\"cancelled\"")) "," BUSTER_RERUN_JOB(4, BUSTER_RERUN_OTHER_ROW "shard 3", "\"cancelled\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"success\"") "," BUSTER_RERUN_STEP("Complete job", "\"success\"")))
                BUSTER_RERUN_ANNOTATION(2, BUSTER_RERUN_FAIL_FAST " \\\"shards.1\\\" failed") BUSTER_RERUN_ANNOTATION(2, "The operation was canceled.") BUSTER_RERUN_ANNOTATION(3, BUSTER_RERUN_FAIL_FAST " \\\"test.x86-64\\\" failed"),
        .decision = "rerun",
        .verdicts = "setup/setup-step setup/setup-step cancelled/fail-fast cancelled/steps-passed",
    },
    {
        .what = "a fail-fast cancellation in tests beside a test failure",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"cancelled\"")))
                BUSTER_RERUN_ANNOTATION(2, BUSTER_RERUN_FAIL_FAST " \\\"shards.1\\\" failed"),
        .decision = "keep",
        .verdicts = "keep/outside-setup cancelled/fail-fast",
    },
    {
        .what = "a timeout in tests beside a setup failure",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"cancelled\"") "," BUSTER_RERUN_STEP("Preserve failed source SSA comparison inputs", "\"success\"")))
                BUSTER_RERUN_ANNOTATION(2, "The job has " BUSTER_RERUN_TIMEOUT " of 2h0m0s") BUSTER_RERUN_ANNOTATION(2, "The operation was canceled."),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/timeout",
    },
    {
        .what = "a timeout that fail-fast's annotation doesn't excuse",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"cancelled\"")))
                BUSTER_RERUN_ANNOTATION(2, BUSTER_RERUN_FAIL_FAST " \\\"shards.1\\\" failed") BUSTER_RERUN_ANNOTATION(2, "The job running on runner GitHub Actions 7 has " BUSTER_RERUN_TIMEOUT " of 120 minutes."),
        .decision = "keep",
        .verdicts = "keep/timeout",
    },
    {
        .what = "a timeout in setup",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", BUSTER_RERUN_SETUP "," BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"cancelled\"") "," BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"skipped\"")))
                BUSTER_RERUN_ANNOTATION(2, "The job has " BUSTER_RERUN_TIMEOUT " of 2h0m0s"),
        .decision = "rerun",
        .verdicts = "setup/setup-step",
    },
    {
        .what = "a cancellation in tests without fail-fast's annotation",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"cancelled\"")))
                BUSTER_RERUN_ANNOTATION(2, "The operation was canceled."),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/interrupted",
    },
    {
        .what = "a fail-fast cancellation in a job whose test step had failed",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"failure\"") "," BUSTER_RERUN_STEP("Preserve failed source SSA comparison inputs", "\"cancelled\"")))
                BUSTER_RERUN_ANNOTATION(2, BUSTER_RERUN_FAIL_FAST " \\\"shards.1\\\" failed"),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/outside-setup",
    },
    {
        .what = "a cancelled job that never started",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) "," BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", "")),
        .decision = "rerun",
        .verdicts = "setup/setup-step cancelled/not-started",
    },
    {
        .what = "cancelled jobs that never started, alone",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(2, BUSTER_RERUN_OTHER_ROW "shard 2", "\"cancelled\"", "")),
        .decision = "keep",
        .verdicts = "cancelled/not-started",
    },
    {
        .what = "the 9700X's job in checkout",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) ",{\"id\":2,\"name\":\"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)\",\"status\":\"completed\",\"conclusion\":\"failure\",\"labels\":[\"self-hosted\",\"Linux\",\"X64\",\"buster-zen5\",\"ryzen-9700x\"],\"steps\":[" BUSTER_RERUN_STEP("Check out triggering commit", "\"failure\"") "]}"),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/not-hosted",
    },
    {
        .what = "a job without labels",
        .json = BUSTER_RERUN_PAGE(1, "{\"id\":1,\"name\":\"x\",\"status\":\"completed\",\"conclusion\":\"failure\",\"steps\":[]}"),
        .decision = "keep",
        .verdicts = "keep/not-hosted",
    },
    {
        .what = "a failed job whose every step passed",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_SETUP "," BUSTER_RERUN_STEP(BUSTER_RERUN_TEST_STEP, "\"success\""))),
        .decision = "keep",
        .verdicts = "keep/no-step-failed",
    },
    {
        .what = "a job still running",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"failure\"", BUSTER_RERUN_STEP("Install official Clang 23.1.4 and test tools", "\"failure\"")) ",{\"id\":2,\"name\":\"y\",\"status\":\"in_progress\",\"conclusion\":null,\"labels\":[\"ubuntu-26.04\"]}"),
        .decision = "keep",
        .verdicts = "setup/setup-step keep/incomplete",
    },
    {
        .what = "a failed run whose jobs all passed",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, "changes", "\"success\"", BUSTER_RERUN_SETUP)),
        .decision = "keep",
        .verdicts = "",
    },
    {
        .what = "fail-fast cancellations alone",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, BUSTER_RERUN_OTHER_ROW "shard 1", "\"cancelled\"", BUSTER_RERUN_STEP("Take the vector item the draw assigns, or end as a redraw", "\"cancelled\"")))
                BUSTER_RERUN_ANNOTATION(1, BUSTER_RERUN_FAIL_FAST " \\\"test.x86-64\\\" failed"),
        .decision = "keep",
        .verdicts = "cancelled/fail-fast",
    },
    {
        .what = "an annotation of a job the pages don't hold",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, "a", "\"cancelled\"", "")) BUSTER_RERUN_ANNOTATION(2, "x"),
    },
    {
        .what = "an annotation without its message",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, "a", "\"cancelled\"", "")) "{\"job_id\":1}",
    },
    {
        .what = "a document both a page and an annotation",
        .json = "{\"total_count\":0,\"jobs\":[],\"job_id\":1,\"message\":\"x\"}",
    },
    {
        .what = "annotations alone",
        .json = BUSTER_RERUN_ANNOTATION(1, "x"),
    },
    {
        .what = "two pages, the reader's other values, escapes and whitespace",
        .json = " {\"total_count\" : 2, \"extra\": {\"a\": [1, -2.5e+3, true, false, null, {\"b\": []}], \"c\": \"\\u00e9\\ud83d\\ude00\"}, \"jobs\": [ " BUSTER_RERUN_JOB(1, "a \\\"quoted\\\"\\tname\\/\\u0041", "\"failure\"", "{\"name\":\"Set up job\",\"conclusion\":\"failure\",\"x\":[[],{}]}") " ] }\n"
                BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(2, "b", "\"skipped\"", "")),
        .decision = "rerun",
        .verdicts = "setup/setup-step",
    },
    {
        .what = "pages that disagree on total_count",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, "a", "\"success\"", "")) BUSTER_RERUN_PAGE(3, BUSTER_RERUN_JOB(2, "b", "\"success\"", "")),
    },
    {
        .what = "fewer jobs than total_count",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, "a", "\"success\"", "")),
    },
    {
        .what = "a job twice",
        .json = BUSTER_RERUN_PAGE(2, BUSTER_RERUN_JOB(1, "a", "\"success\"", "") "," BUSTER_RERUN_JOB(1, "a", "\"success\"", "")),
    },
    {
        .what = "no page",
        .json = " \n",
    },
    {
        .what = "a page without total_count",
        .json = "{\"jobs\":[]}",
    },
    {
        .what = "a page without jobs",
        .json = "{\"total_count\":0}",
    },
    {
        .what = "an array as a page",
        .json = "[]",
    },
    {
        .what = "a job without a status",
        .json = BUSTER_RERUN_PAGE(1, "{\"id\":1,\"name\":\"a\"}"),
    },
    {
        .what = "a step without a name",
        .json = BUSTER_RERUN_PAGE(1, BUSTER_RERUN_JOB(1, "a", "\"failure\"", "{\"conclusion\":\"failure\"}")),
    },
    {
        .what = "a string as a job",
        .json = BUSTER_RERUN_PAGE(1, "\"a\""),
    },
    {
        .what = "an object as a label",
        .json = BUSTER_RERUN_PAGE(1, "{\"id\":1,\"name\":\"a\",\"status\":\"completed\",\"labels\":[{}]}"),
    },
    {
        .what = "a number as a name",
        .json = BUSTER_RERUN_PAGE(1, "{\"id\":1,\"name\":7,\"status\":\"completed\"}"),
    },
    {
        .what = "a negative id",
        .json = BUSTER_RERUN_PAGE(1, "{\"id\":-1,\"name\":\"a\",\"status\":\"completed\"}"),
    },
    {
        .what = "a trailing comma",
        .json = "{\"total_count\":0,\"jobs\":[],}",
    },
    {
        .what = "a truncated page",
        .json = "{\"total_count\":0,\"jobs\":[",
    },
    {
        .what = "an invalid escape",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":\"\\x\"}",
    },
    {
        .what = "a NUL in a string",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":\"\\u0000\"}",
    },
    {
        .what = "a lone surrogate",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":\"\\udc00\"}",
    },
    {
        .what = "a control character in a string",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":\"\t\"}",
    },
    {
        .what = "a number without digits",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":-}",
    },
    {
        .what = "a fraction without digits",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":1.}",
    },
    {
        .what = "a misspelt literal",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":nul}",
    },
    {
        .what = "a fractional total_count",
        .json = "{\"total_count\":0.5,\"jobs\":[]}",
    },
    {
        .what = "a missing colon",
        .json = "{\"total_count\" 0,\"jobs\":[]}",
    },
    {
        .what = "an unclosed page before another",
        .json = "{\"total_count\":0,\"jobs\":[]{\"total_count\":0,\"jobs\":[]}",
    },
    {
        .what = "nesting past the limit",
        .json = "{\"total_count\":0,\"jobs\":[],\"a\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}",
    },
};

// The verdicts of the stopped jobs as `verdict/reason` separated by spaces.
static void rerun_verdict_text(const Run* run, const Outcome* outcomes, char* text, size_t capacity)
{
    size_t used = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < run->job_count; i += 1)
    {
        if (outcomes[i].verdict != VERDICT_NONE)
        {
            const int written = snprintf(text + used, capacity - used, "%s%s/%s", used != 0 ? " " : "", rerun_verdicts[outcomes[i].verdict], rerun_reasons[outcomes[i].reason].token);
            used += written > 0 && (size_t)written < capacity - used ? (size_t)written : 0;
        }
    }
}

static int rerun_test(const char* root, Run* run, Outcome* outcomes)
{
    Policy policy = { 0 };
    for (size_t i = 0; i < sizeof(rerun_cases) / sizeof(rerun_cases[0]); i += 1)
    {
        const Case* test = &rerun_cases[i];
        const char* error = NULL;
        const bool read = rerun_read(run, test->json, strlen(test->json), &error);
        char what[2048];
        snprintf(what, sizeof(what), "%s: %s", test->what, read ? "read" : error != NULL ? error : "refused");
        rerun_expect(&policy, read == (test->decision != NULL), what);
        if (read && test->decision != NULL)
        {
            const Decision decision = rerun_classify(run, outcomes);
            char verdicts[1024];
            rerun_verdict_text(run, outcomes, verdicts, sizeof(verdicts));
            const bool expected = strcmp(decision.rerun ? "rerun" : "keep", test->decision) == 0 && strcmp(verdicts, test->verdicts) == 0;
            snprintf(what, sizeof(what), "%s: %s, %s", test->what, decision.rerun ? "rerun" : "keep", verdicts);
            rerun_expect(&policy, expected, what);
        }
    }
    // The reader's decoding: escapes, a surrogate pair and the labels.
    const char* error = NULL;
    const Case* decoding = &rerun_cases[sizeof(rerun_cases) / sizeof(rerun_cases[0]) - 1];
    for (size_t i = 0; i < sizeof(rerun_cases) / sizeof(rerun_cases[0]); i += 1)
    {
        decoding = strncmp(rerun_cases[i].what, "two pages", 9) == 0 ? &rerun_cases[i] : decoding;
    }
    const bool read = rerun_read(run, decoding->json, strlen(decoding->json), &error);
    rerun_expect(&policy, read && run->pages == 2 && run->job_count == 2 && strcmp(run->jobs[0].name, "a \"quoted\"\tname/A") == 0, "the reader decodes a name's escapes");
    rerun_expect(&policy, read && run->jobs[0].ubuntu && !run->jobs[0].self_hosted && run->jobs[0].step_count == 1 && run->jobs[1].step_count == 0, "the reader keeps each job's labels and steps");
    // Only a cancelled job interrupted outside setup needs its annotations: the draw's candidate of the fail-fast case.
    const Case* fail_fast = NULL;
    for (size_t i = 0; i < sizeof(rerun_cases) / sizeof(rerun_cases[0]); i += 1)
    {
        fail_fast = strncmp(rerun_cases[i].what, "fail-fast cancellations in setup", 32) == 0 ? &rerun_cases[i] : fail_fast;
    }
    const bool fail_fast_read = fail_fast != NULL && rerun_read(run, fail_fast->json, strlen(fail_fast->json), &error);
    uint64_t needed = 0;
    uint32_t needed_count = 0;
    for (uint32_t i = 0; fail_fast_read && i < run->job_count; i += 1)
    {
        // Without the annotations, as tools/ci/rerun.sh asks before it fetches them.
        run->jobs[i].fail_fast = false;
        run->jobs[i].timeout = false;
    }
    const Decision unannotated = fail_fast_read ? rerun_classify(run, outcomes) : (Decision){ 0 };
    for (uint32_t i = 0; fail_fast_read && i < run->job_count; i += 1)
    {
        if (rerun_needs_annotations(run, outcomes, unannotated, i))
        {
            needed = run->jobs[i].id;
            needed_count += 1;
        }
    }
    rerun_expect(&policy, fail_fast_read && needed_count == 1 && needed == 3, "only a cancelled job interrupted outside setup needs its annotations");
    // A run that a test failure keeps needs none: annotations could excuse only its cancelled jobs.
    const Case* kept = NULL;
    for (size_t i = 0; i < sizeof(rerun_cases) / sizeof(rerun_cases[0]); i += 1)
    {
        kept = strcmp(rerun_cases[i].what, "a fail-fast cancellation in tests beside a test failure") == 0 ? &rerun_cases[i] : kept;
    }
    const bool kept_read = kept != NULL && rerun_read(run, kept->json, strlen(kept->json), &error);
    for (uint32_t i = 0; kept_read && i < run->job_count; i += 1)
    {
        run->jobs[i].fail_fast = false;
    }
    const Decision kept_decision = kept_read ? rerun_classify(run, outcomes) : (Decision){ 0 };
    bool kept_needs = false;
    for (uint32_t i = 0; kept_read && i < run->job_count; i += 1)
    {
        kept_needs = kept_needs || rerun_needs_annotations(run, outcomes, kept_decision, i);
    }
    rerun_expect(&policy, kept_read && !kept_needs, "a run a test failure keeps needs no annotations");
    // Each allowlisted name is listed once; the derived steps are what the rule resolves.
    for (size_t i = 0; i < BUSTER_RERUN_STEP_COUNT; i += 1)
    {
        uint32_t copies = 0;
        for (size_t j = 0; j < BUSTER_RERUN_STEP_COUNT; j += 1)
        {
            copies += strcmp(rerun_steps[i].name, rerun_steps[j].name) == 0;
        }
        rerun_expect(&policy, copies == 1, "each allowlisted step is listed once");
    }
    if (root != NULL)
    {
        rerun_policy(&policy, root);
    }
    printf("rerun --self-test: %u checks, %u failures\n", policy.checks, policy.failures);
    return policy.failures != 0;
}

int main(int argc, char** argv)
{
    int status = 2;
    Run* run = calloc(1, sizeof(Run));
    Step* steps = calloc(BUSTER_RERUN_STEP_LIMIT, sizeof(Step));
    Outcome* outcomes = calloc(BUSTER_RERUN_JOB_LIMIT, sizeof(Outcome));
    Annotation* annotations = calloc(BUSTER_RERUN_ANNOTATION_LIMIT, sizeof(Annotation));
    const bool allocated = run != NULL && steps != NULL && outcomes != NULL && annotations != NULL;
    if (allocated)
    {
        run->steps = steps;
        run->annotations = annotations;
    }
    if (!allocated)
    {
        fputs("rerun: out of memory\n", stderr);
    }
    else if (argc >= 2 && strcmp(argv[1], "--self-test") == 0 && (argc == 2 || (argc == 4 && strcmp(argv[2], "--root") == 0)))
    {
        status = rerun_test(argc == 4 ? argv[3] : NULL, run, outcomes);
    }
    else if (argc >= 3 && (strcmp(argv[1], "classify") == 0 || strcmp(argv[1], "annotations") == 0))
    {
        // classify [--markdown FILE] PAGES...: the pages' files, concatenated, are one stream of JSON documents.
        // annotations PAGES...: the IDs of the jobs whose annotations could change their verdicts, a line each.
        const bool annotations = strcmp(argv[1], "annotations") == 0;
        int first = 2;
        const char* markdown = NULL;
        if (!annotations && argc >= 5 && strcmp(argv[2], "--markdown") == 0)
        {
            markdown = argv[3];
            first = 4;
        }
        char* input = NULL;
        size_t length = 0;
        bool ok = first < argc;
        for (int i = first; ok && i < argc; i += 1)
        {
            size_t file_length = 0;
            char* file = rerun_read_file(argv[i], &file_length);
            ok = file != NULL && length + file_length + 1 <= BUSTER_RERUN_INPUT_LIMIT;
            char* grown = ok ? realloc(input, length + file_length + 2) : NULL;
            ok = grown != NULL;
            if (ok)
            {
                input = grown;
                memcpy(input + length, file, file_length);
                length += file_length;
                input[length] = '\n';
                length += 1;
                input[length] = 0;
            }
            else
            {
                fprintf(stderr, "rerun: can't read %s\n", argv[i]);
            }
            free(file);
        }
        const char* error = NULL;
        ok = ok && rerun_read(run, input, length, &error);
        if (ok && annotations)
        {
            const Decision decision = rerun_classify(run, outcomes);
            for (uint32_t i = 0; i < run->job_count; i += 1)
            {
                if (rerun_needs_annotations(run, outcomes, decision, i))
                {
                    printf("%llu\n", (unsigned long long)run->jobs[i].id);
                }
            }
            status = fflush(stdout) == 0 ? 0 : 1;
        }
        else if (ok)
        {
            const Decision decision = rerun_classify(run, outcomes);
            rerun_print_report(stdout, run, outcomes, decision);
            FILE* out = markdown != NULL ? fopen(markdown, "w") : NULL;
            if (out != NULL)
            {
                rerun_print_markdown(out, run, outcomes);
                ok = fclose(out) == 0;
            }
            ok = ok && (markdown == NULL || out != NULL);
            status = ok && fflush(stdout) == 0 ? 0 : 1;
        }
        else
        {
            fprintf(stderr, "rerun: invalid jobs API response: %s\n", error != NULL ? error : "unreadable");
            status = 1;
        }
        free(input);
    }
    else
    {
        fputs("usage: rerun --self-test [--root DIR] | rerun classify [--markdown FILE] PAGE.json... | rerun annotations PAGE.json...\n", stderr);
    }
    if (run != NULL)
    {
        free(run->arena);
    }
    free(annotations);
    free(outcomes);
    free(steps);
    free(run);
    return status;
}
