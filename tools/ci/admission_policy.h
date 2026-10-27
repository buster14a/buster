// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Version 1 policy: metadata in, one admission out. The network adapter owns authentication and persistence.
// This is a bounded control plane (at most 64 open PRs), outside the compiler and measurement paths.
// Scalar scans follow GitHub's ordered review/command streams; HTTP round trips dominate their cost.

#define BUSTER_CI_ITEMS 64
#define BUSTER_CI_SHA 41
#define BUSTER_CI_LEASE_SECONDS (40 * 60)
#define BUSTER_CI_DISPATCH_SECONDS 180
#define BUSTER_CI_MAIN_BOUND (2 * 60 * 60)
#define BUSTER_CI_AGING_SECONDS (30 * 60)
#define BUSTER_CI_FAIR_EVERY 8
#define BUSTER_CI_BACKLOG 3
#define BUSTER_CI_SIGNAL_REFRESH_SECONDS (15 * 60)
#define BUSTER_CI_REQUIRED "Build and execute on Ryzen 7 9700X"
#define BUSTER_CI_EXECUTOR "9700X admitted execution"
#define BUSTER_CI_STATE_NAME "9700X controller state v1"
#define BUSTER_CI_STATE_ID "buster-9700x/state/v1"
// An immutable existing commit, rather than a moving branch or a fork-controlled artifact/cache.
#define BUSTER_CI_STATE_SHA "335424b18635fa9ab3ac5eb91e7497aaddd37cab"
#define BUSTER_CI_EXECUTOR_REF "buster14a/buster/.github/workflows/9700x.yml@refs/heads/main"

static bool ci_host_source_allowed(const char* repository, const char* workflow_ref, const char* ref)
{
    bool allowed = *repository != 0;
    if (strcmp(repository, "buster14a/buster") == 0)
    {
        allowed = strcmp(workflow_ref, BUSTER_CI_EXECUTOR_REF) == 0 && strcmp(ref, "refs/heads/main") == 0;
    }
    return allowed;
}

typedef enum CiCommandKind
{
    CI_COMMAND_NONE,
    CI_COMMAND_APPROVE,
    CI_COMMAND_PRIORITY,
    CI_COMMAND_HOLD,
    CI_COMMAND_RESUME,
    CI_COMMAND_RETRY,
    CI_COMMAND_STATUS
} CiCommandKind;

typedef struct CiCommand CiCommand;
struct CiCommand
{
    CiCommandKind kind;
    int priority;
    uint64_t run;
};

static bool ci_sha(const char* string)
{
    bool valid = strlen(string) == 40;
    for (size_t i = 0; valid && i < 40; ++i)
    {
        valid = (string[i] >= '0' && string[i] <= '9') || (string[i] >= 'a' && string[i] <= 'f');
    }
    return valid;
}

static bool ci_trusted(const char* permission)
{
    return strcmp(permission, "admin") == 0 || strcmp(permission, "maintain") == 0 || strcmp(permission, "write") == 0;
}

static int ci_priority(const char* word)
{
    int result = -2;
    if (strcmp(word, "critical") == 0)
    {
        result = 0;
    }
    else if (strcmp(word, "high") == 0)
    {
        result = 1;
    }
    else if (strcmp(word, "normal") == 0 || strcmp(word, "clear") == 0)
    {
        result = -1;
    }
    else if (strcmp(word, "low") == 0)
    {
        result = 4;
    }
    return result;
}

static const char* ci_priority_name(int priority)
{
    const char* names[] = {"P0 critical", "P1 high / approved", "P2 normal", "P3 main/background", "P4 low"};
    return priority >= 0 && priority <= 4 ? names[priority] : "automatic";
}

static const char* ci_command_name(CiCommandKind kind)
{
    const char* names[] = {"invalid", "approve", "priority", "hold", "resume", "retry", "status"};
    return kind >= CI_COMMAND_NONE && kind <= CI_COMMAND_STATUS ? names[kind] : "invalid";
}

// Commands/explicit approvals occupy the entire comment, apart from blank lines and at most three leading spaces.
// This intentionally conservative grammar rejects quotations, fences, code indentation, prose and negations.
static CiCommand ci_command(const char* body)
{
    CiCommand result = {0};
    result.priority = -1;
    size_t length = strlen(body);
    const char* start = body;
    while (*start == '\r' || *start == '\n')
    {
        ++start;
    }
    size_t indentation = 0;
    while (start[indentation] == ' ')
    {
        ++indentation;
    }
    bool valid = indentation < 4 && length < 256;
    start += indentation;
    size_t size = strlen(start);
    while (size != 0 && (start[size - 1] == ' ' || start[size - 1] == '\r' || start[size - 1] == '\n'))
    {
        --size;
    }
    char line[256] = {0};
    if (valid)
    {
        memcpy(line, start, size);
        valid = memchr(line, '\n', size) == NULL && memchr(line, '\r', size) == NULL && memchr(line, '\t', size) == NULL;
    }
    if (valid && (strcmp(line, "LGTM") == 0 || strcmp(line, "APPROVED") == 0 || strcmp(line, "SHIP IT") == 0 || strcmp(line, "READY TO MERGE") == 0 || strcmp(line, "/ci approve") == 0))
    {
        result.kind = CI_COMMAND_APPROVE;
    }
    else if (valid && strncmp(line, "/ci ", 4) == 0)
    {
        char* words[8] = {0};
        size_t count = 0;
        char* save = NULL;
        for (char* word = strtok_r(line + 4, " ", &save); word != NULL && count < 8; word = strtok_r(NULL, " ", &save))
        {
            words[count++] = word;
        }
        size_t used = 1;
        if (count != 0 && strcmp(words[0], "priority") == 0 && count >= 2)
        {
            result.kind = CI_COMMAND_PRIORITY;
            result.priority = ci_priority(words[1]);
            valid = result.priority != -2;
            used = 2;
        }
        else if (count != 0 && strcmp(words[0], "hold") == 0)
        {
            result.kind = CI_COMMAND_HOLD;
        }
        else if (count != 0 && strcmp(words[0], "resume") == 0)
        {
            result.kind = CI_COMMAND_RESUME;
        }
        else if (count != 0 && strcmp(words[0], "retry") == 0)
        {
            result.kind = CI_COMMAND_RETRY;
        }
        else if (count != 0 && strcmp(words[0], "status") == 0)
        {
            result.kind = CI_COMMAND_STATUS;
        }
        else
        {
            valid = false;
        }
        if (valid && count == used + 2 && strcmp(words[used], "run") == 0)
        {
            const char* id = words[used + 1];
            uint64_t number = 0;
            for (const char* at = id; *at != 0; ++at)
            {
                unsigned digit = (unsigned char)*at - '0';
                if (digit > 9 || number > (UINT64_MAX - digit) / 10)
                {
                    valid = false;
                    break;
                }
                number = number * 10 + digit;
            }
            result.run = number;
            valid = valid && number != 0;
        }
        else
        {
            valid = valid && count == used;
        }
    }
    if (!valid)
    {
        result.kind = CI_COMMAND_NONE;
    }
    return result;
}

typedef struct CiSettingOrder CiSettingOrder;
struct CiSettingOrder
{
    uint64_t at;
    uint64_t id;
    uint64_t key;
};

typedef struct CiPr CiPr;
struct CiPr
{
    uint64_t number;
    uint64_t head_repo;
    unsigned frozen_state;
    char frozen_merge[BUSTER_CI_SHA];
    char frozen_engine[96];
    uint64_t frozen_retry_at;
    char head[BUSTER_CI_SHA];
    char base[BUSTER_CI_SHA];
    uint64_t seen_at;
    uint64_t queued_at;
    int manual;
    bool held;
    uint64_t command_at;
    uint64_t command_id;
    uint64_t command_hash;
    CiSettingOrder setting_order[2];
    char audit[256];
    uint64_t check;
    char check_base[BUSTER_CI_SHA];
    uint64_t check_hash;
    // 0: pending, 1: qualified, 2: failed (explicit retry or a new base/head is required).
    unsigned outcome;
    uint64_t request_run;
    uint64_t approval_at;
    uint64_t admitted_at;
    uint64_t completed_at;
    uint64_t active_seconds;
    char status_state[16];
    int effective_priority;
    uint64_t queue_position;
    bool eligible;
    char reason[128];
    uint64_t executor_run;
    uint64_t signals_at;
    bool cached_approved;
    uint64_t signal_approval_at;
    bool cached_code;
    char code_merge[BUSTER_CI_SHA];
};

typedef struct CiOverride CiOverride;
struct CiOverride
{
    uint64_t run;
    uint64_t pr;
    char head[BUSTER_CI_SHA];
    int manual;
    bool held;
    bool retry;
    uint64_t queued_at;
    CiSettingOrder setting_order[2];
};

typedef struct CiLease CiLease;
struct CiLease
{
    uint64_t pr;
    uint64_t run;
    uint64_t attempt;
    char nonce[80];
    char head[BUSTER_CI_SHA];
    char base[BUSTER_CI_SHA];
    char merge[BUSTER_CI_SHA];
    char workflow_sha[BUSTER_CI_SHA];
    uint64_t admitted_at;
    uint64_t expires_at;
    bool revoking;
    char phase[48];
};

typedef struct CiState CiState;
struct CiState
{
    uint64_t check;
    uint64_t generation;
    uint64_t dispatch_event;
    unsigned command_order_version;
    unsigned since_fair;
    CiLease lease;
    CiPr prs[BUSTER_CI_ITEMS];
    size_t pr_count;
    CiOverride overrides[BUSTER_CI_ITEMS];
    size_t override_count;
    char* saved_json;
};

typedef struct CiIdentity CiIdentity;
struct CiIdentity
{
    uint64_t number;
    uint64_t head_repo;
    char head[BUSTER_CI_SHA];
    char base[BUSTER_CI_SHA];
    char merge[BUSTER_CI_SHA];
    // -1: UNKNOWN, 0: CONFLICTING, 1: MERGEABLE. Behind with no conflict is MERGEABLE.
    int mergeable;
    bool open;
    bool fork;
    bool code;
    bool approved;
    bool frozen;
    bool preflight_blocked;
    uint64_t approval_at;
};

static bool ci_same_pair(const CiIdentity* left, const CiIdentity* right)
{
    return left->number == right->number && strcmp(left->head, right->head) == 0 && strcmp(left->base, right->base) == 0 && left->open == right->open;
}

static bool ci_lease_current(const CiLease* lease, const CiIdentity* current)
{
    return current->open && current->mergeable == 1 && lease->pr == current->number && strcmp(lease->head, current->head) == 0 && strcmp(lease->base, current->base) == 0 && strcmp(lease->merge, current->merge) == 0;
}

static void ci_observe(CiPr* state, const CiIdentity* current, uint64_t now)
{
    state->head_repo = current->head_repo;
    bool head_changed = strcmp(state->head, current->head) != 0;
    bool base_changed = strcmp(state->base, current->base) != 0;
    if (head_changed)
    {
        ci_copy(state->head, sizeof(state->head), current->head);
        state->seen_at = now;
        state->queued_at = now;
        state->check = 0;
        state->check_hash = 0;
        state->status_state[0] = 0;
        state->request_run = 0;
        state->approval_at = 0;
        state->executor_run = 0;
        state->signals_at = 0;
        state->cached_approved = false;
        state->admitted_at = 0;
        state->completed_at = 0;
    }
    if (head_changed || base_changed)
    {
        state->frozen_state = 0;
        state->frozen_merge[0] = 0;
        state->frozen_engine[0] = 0;
        state->frozen_retry_at = 0;
        ci_copy(state->base, sizeof(state->base), current->base);
        state->outcome = 0;
        state->admitted_at = 0;
        state->completed_at = 0;
        state->active_seconds = 0;
    }
}

static bool ci_comment_current(const CiPr* state, uint64_t updated_at)
{
    // The API does not attach conversation comments to commits. Never guess across an unobserved head change.
    // Same-second events are conservative too; an edit/repost after observation binds the new revision explicitly.
    return updated_at > state->seen_at;
}

static bool ci_command_new(const CiPr* state, uint64_t updated_at, uint64_t id)
{
    return updated_at > state->command_at || (updated_at == state->command_at && id > state->command_id);
}

typedef struct CiCandidate CiCandidate;
struct CiCandidate
{
    uint64_t pr;
    uint64_t run;
    uint64_t attempt;
    uint64_t queued_at;
    int priority;
    bool eligible;
    bool background;
    const char* reason;
};

static CiCandidate ci_pr_candidate(const CiPr* pr, const CiIdentity* current, const CiOverride* override)
{
    CiCandidate result = {0};
    result.pr = pr->number;
    result.run = pr->request_run;
    result.queued_at = pr->queued_at;
    result.priority = current->approved ? 1 : 2;
    result.reason = current->approved ? "trusted current-revision approval" : "ordinary PR";
    if (pr->manual >= 0)
    {
        result.priority = pr->manual;
        result.reason = "maintainer PR override";
    }
    if (override != NULL && override->manual >= 0)
    {
        result.priority = override->manual;
        result.reason = "maintainer run override";
    }
    result.eligible = current->open && current->mergeable == 1 && current->code && pr->outcome == 0 && !pr->held && (override == NULL || !override->held) && (!current->fork || current->approved);
    if (!current->open)
    {
        result.reason = "PR closed";
    }
    else if (current->mergeable == 0)
    {
        result.reason = "confirmed merge conflict";
    }
    else if (current->mergeable < 0)
    {
        result.reason = current->preflight_blocked ? "merge preflight budget/error; awaiting retry or /ci retry" : "mergeability UNKNOWN; awaiting a stable head/base pair";
    }
    else if (!current->code)
    {
        result.reason = "documentation only";
    }
    else if (pr->held || (override != NULL && override->held))
    {
        result.reason = "held by a maintainer";
    }
    else if (current->fork && !current->approved)
    {
        result.reason = "fork revision needs trusted explicit approval";
    }
    else if (pr->outcome == 1)
    {
        result.reason = "qualified at this head and base";
    }
    else if (pr->outcome == 2)
    {
        result.reason = "qualification failed; /ci retry requests another attempt";
    }
    return result;
}

static bool ci_older(const CiCandidate* left, const CiCandidate* right)
{
    return left->queued_at < right->queued_at || (left->queued_at == right->queued_at && (left->pr < right->pr || (left->pr == right->pr && left->run < right->run)));
}

// One decision for a pool whose enforced capacity is one. Deadlines/fair slots bound starvation without preemption.
// A main/daily deadline comes first. After eight ordinary admissions, an aged PR gets one FIFO service slot.
// All other slots follow priority, then FIFO. No completion/lease expiry alone permits a second live executor.
static size_t ci_choose(const CiCandidate* candidates, size_t count, uint64_t now, unsigned since_fair, unsigned threshold)
{
    size_t selected = SIZE_MAX;
    size_t deadline = SIZE_MAX;
    size_t fair = SIZE_MAX;
    unsigned prs = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const CiCandidate* candidate = &candidates[i];
        if (candidate->eligible)
        {
            prs += candidate->pr != 0;
            bool due = now >= candidate->queued_at && now - candidate->queued_at >= BUSTER_CI_MAIN_BOUND;
            if (candidate->background && due && (deadline == SIZE_MAX || ci_older(candidate, &candidates[deadline])))
            {
                deadline = i;
            }
            bool aged = now >= candidate->queued_at && now - candidate->queued_at >= BUSTER_CI_AGING_SECONDS;
            if (candidate->pr != 0 && aged && (fair == SIZE_MAX || ci_older(candidate, &candidates[fair])))
            {
                fair = i;
            }
        }
    }
    int selected_priority = 99;
    for (size_t i = 0; i < count; ++i)
    {
        const CiCandidate* candidate = &candidates[i];
        if (candidate->eligible)
        {
            int priority = candidate->priority;
            if (candidate->background && priority == 3 && prs < threshold)
            {
                priority = 2;
            }
            if (selected == SIZE_MAX || priority < selected_priority || (priority == selected_priority && ci_older(candidate, &candidates[selected])))
            {
                selected = i;
                selected_priority = priority;
            }
        }
    }
    if (deadline != SIZE_MAX)
    {
        selected = deadline;
    }
    else if (since_fair >= BUSTER_CI_FAIR_EVERY && fair != SIZE_MAX)
    {
        selected = fair;
    }
    return selected;
}

static unsigned ci_fair_after(unsigned counter, const CiCandidate* candidate, uint64_t now)
{
    bool fair = counter >= BUSTER_CI_FAIR_EVERY && candidate->pr != 0 && now >= candidate->queued_at && now - candidate->queued_at >= BUSTER_CI_AGING_SECONDS;
    return fair ? 0 : counter < BUSTER_CI_FAIR_EVERY ? counter + 1 : counter;
}

static void ci_mark_admitted(CiCandidate* candidate, const CiLease* lease)
{
    bool current = *lease->nonce != 0 && ((candidate->pr != 0 && candidate->pr == lease->pr) || (candidate->pr == 0 && candidate->run != 0 && candidate->run == lease->run));
    if (current)
    {
        candidate->eligible = false;
        candidate->reason = "admitted work retains the non-preemptible pool lease";
    }
}
