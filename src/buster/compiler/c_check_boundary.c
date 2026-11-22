// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// SCAFFOLD (#2850's redesign, #2524's body boundary; not for merge): a function body's lowered function supplied from
// outside the reader, and the recording of an ordinary compile that supplies it.
//
// The boundary: a supplied body's tokens are masked from the reader as a lazily deferred body's are (CheckInput.
// deferred_tokens), but its function is still defined. What the unit needs of it comes from the supplier:
//   - its lowered function (Tier0Function), as its part has it after the ABI wiring and before check_lower_finish, the
//     symbols it names re-expressed as objects (declaration tokens + 1) and runtime functions;
//   - the objects it references (check_program_references' list for it), which are what the symbols' numbering and the
//     selection read of a body;
//   - its string tokens in the reader's order, whose records the data emission orders and merges by.
// A body is refused (read as ever) where it makes unit-level state this doesn't carry: see CheckBoundaryRefusal.
//
// The supplier here is a recording: BUSTER_BOUNDARY_RECORD=DIRECTORY makes an ordinary compile write each unit's bodies
// there, and BUSTER_BOUNDARY_SUPPLY=DIRECTORY (with BUSTER_BOUNDARY_SELECT=N:K, the eligible bodies whose ordinal modulo
// N is K, every one by default) makes a compile of the same unit with the same options take them. It is what the stream
// (#2850) is developed against, body by body: the unit's object with the bodies supplied is the ordinary one.

#define BUSTER_CHECK_BOUNDARY_MAGIC 0x444e4242u
#define BUSTER_CHECK_BOUNDARY_VERSION 1u

// Why a body is not supplied; 0 for one that is.
typedef enum CheckBoundaryRefusal
{
    CHECK_BOUNDARY_SUPPLIED,
    // Its function has columns or rows this doesn't carry yet.
    CHECK_BOUNDARY_ASM,
    CHECK_BOUNDARY_LABELS,
    CHECK_BOUNDARY_TAILS,
    // It names a symbol that is neither an object's nor a runtime function's: a helper's, an initializer pool's, a jump
    // table's, errno's.
    CHECK_BOUNDARY_HELPER,
    CHECK_BOUNDARY_POOL,
    CHECK_BOUNDARY_TABLE,
    CHECK_BOUNDARY_ERRNO,
    CHECK_BOUNDARY_SYMBOL,
    // A token of it is an object with a symbol and no string literal: a block-scope static, a first extern or implicit
    // declaration, a label whose address is taken, a function's name's array.
    CHECK_BOUNDARY_OBJECT,
    // A token of it declares with linkage or static storage.
    CHECK_BOUNDARY_DECLARATION,
    // A builtin's node is in it (the helpers' and errno's keys are counted from the unit's builtin nodes).
    CHECK_BOUNDARY_BUILTIN,
    // A diagnostic is in it.
    CHECK_BOUNDARY_DIAGNOSTIC,
    // The unit's options: -g's rows.
    CHECK_BOUNDARY_SOURCES,
    CHECK_BOUNDARY_REFUSALS,
} CheckBoundaryRefusal;

static const char* const check_boundary_refusal_names[CHECK_BOUNDARY_REFUSALS] = {
    "supplied", "asm", "labels", "tails", "helper", "pool", "table", "errno", "symbol", "object", "declaration", "builtin", "diagnostic", "sources",
};

// How a recorded function names what is outside it: where the name is (an entry's value, a call's target, the hidden
// copy), and what it is.
typedef enum CheckBoundaryPlace
{
    CHECK_BOUNDARY_PLACE_VALUE,
    CHECK_BOUNDARY_PLACE_CALL,
    CHECK_BOUNDARY_PLACE_HIDDEN_COPY,
} CheckBoundaryPlace;

typedef enum CheckBoundaryName
{
    CHECK_BOUNDARY_NAME_OBJECT,
    CHECK_BOUNDARY_NAME_RUNTIME,
    // A jump table of the body's: the payload is its place among the body's own.
    CHECK_BOUNDARY_NAME_TABLE,
} CheckBoundaryName;

// A jump table of a supplied body (CheckLowerTable's, without what only its lowering reads): its entries are each value's
// label among the table's own, from 0.
typedef struct CheckBoundaryTable CheckBoundaryTable;
struct CheckBoundaryTable
{
    uint32_t count;
    uint32_t label_count;
    uint32_t address;
    uint32_t fallback;
    uint64_t lower;
};

typedef struct CheckBoundaryFixup CheckBoundaryFixup;
struct CheckBoundaryFixup
{
    uint32_t place;
    uint32_t index;
    uint32_t name;
    uint32_t payload;
};

typedef struct CheckBoundaryString CheckBoundaryString;
struct CheckBoundaryString
{
    uint32_t token;
    // Its record's index in the recording's reader (CheckInput.strings), which the supplied read must give it too.
    uint32_t index;
};

// A use, in a body, of a declaration outside it (a parameter of its function's, a file-scope declaration): the
// declaration's token, and whether the use reads it (not only sets it). The reader's unused analysis has these of a
// body it reads from its binding events; of a supplied body, from the supplier.
typedef struct CheckBoundaryUse CheckBoundaryUse;
struct CheckBoundaryUse
{
    uint32_t target;
    uint32_t read;
};

// A recording's use as the reader meets it: its body's place among the unit's bodies too.
typedef struct CheckBoundaryMetUse CheckBoundaryMetUse;
struct CheckBoundaryMetUse
{
    uint32_t body;
    uint32_t target;
    uint32_t read;
};

typedef struct CheckBoundaryRange CheckBoundaryRange;
struct CheckBoundaryRange
{
    uint32_t opening;
    uint32_t closing;
};

typedef struct CheckBoundaryBody CheckBoundaryBody;
struct CheckBoundaryBody
{
    uint32_t opening;
    uint32_t closing;
    uint32_t declaration;
    uint32_t refusal;
    uint32_t entries;
    // Whether this compile took it (a supplying compile's selection among the recording's supplied bodies).
    bool taken;
    uint32_t reference_count;
    const uint32_t* references;
    uint32_t string_count;
    const CheckBoundaryString* strings;
    // The runtime functions it calls, in its first calls' order (CheckLowerRuntime).
    uint32_t runtime_count;
    const uint32_t* runtimes;
    uint32_t fixup_count;
    const CheckBoundaryFixup* fixups;
    // Its switches' jump tables, in its controls' order, and their entries one table's after another's.
    uint32_t table_count;
    const CheckBoundaryTable* tables;
    const uint32_t* table_entries;
    // Its uses of declarations outside it, each once.
    uint32_t use_count;
    const CheckBoundaryUse* uses;
    // Its Tier0Function and its columns (c_check_boundary_lower.c), in the recording's bytes.
    void* function;
};

typedef enum CheckBoundaryMode
{
    CHECK_BOUNDARY_OFF,
    CHECK_BOUNDARY_RECORDING,
    CHECK_BOUNDARY_SUPPLYING,
} CheckBoundaryMode;

typedef struct CheckBoundary CheckBoundary;
struct CheckBoundary
{
    CheckBoundaryMode mode;
    const char* directory;
    char path[768];
    // A supplying compile's selection: the supplied bodies whose ordinal modulo `modulus` is `residue`, of those from
    // ordinal `first` to before `end` (BUSTER_BOUNDARY_RANGE=FIRST:END, every one by default).
    uint32_t modulus;
    uint32_t residue;
    uint32_t first;
    uint32_t end;
    uint32_t token_count;
    CheckBoundaryBody* bodies;
    uint32_t body_count;
    uint32_t body_capacity;
    // A recording's bytes as they are written, and a supplying compile's as read.
    uint8_t* bytes;
    uint64_t length;
    uint64_t capacity;
    // Why the unit's supply was dropped whole (a supplying compile whose unit doesn't match its recording), or NULL.
    const char* dropped;
    uint32_t taken;
    uint64_t taken_tokens;
    uint64_t load_ticks;
    // Every body of the unit's parse, by its tokens, in their order (check_boundary_ranges): what a use or a diagnostic
    // is in.
    CheckBoundaryRange* ranges;
    uint32_t range_count;
    // A recording's uses as the reader met them, and the tokens whose bodies a diagnostic that isn't at them refuses
    // (the first use an undefined internal function's warning notes).
    CheckBoundaryMetUse* met_uses;
    uint32_t met_use_count;
    uint32_t met_use_capacity;
    bool met_uses_sorted;
    uint32_t* refused_tokens;
    uint32_t refused_token_count;
    uint32_t refused_token_capacity;
};

static CheckBoundary check_boundary;

static bool check_boundary_recording(void)
{
    return check_boundary.mode == CHECK_BOUNDARY_RECORDING;
}

static bool check_boundary_supplying(void)
{
    return check_boundary.mode == CHECK_BOUNDARY_SUPPLYING && check_boundary.dropped == NULL;
}

// The recording's body whose opening is `opening`, or NULL: the bodies are in their openings' order.
static CheckBoundaryBody* check_boundary_find(uint32_t opening)
{
    uint32_t low = 0;
    uint32_t high = check_boundary.body_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (check_boundary.bodies[middle].opening < opening)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < check_boundary.body_count && check_boundary.bodies[low].opening == opening ? &check_boundary.bodies[low] : NULL;
}

// The body a supplying compile took at `opening`, or NULL.
static const CheckBoundaryBody* check_boundary_taken(uint32_t opening)
{
    const CheckBoundaryBody* body = check_boundary_supplying() ? check_boundary_find(opening) : NULL;
    return body != NULL && body->taken ? body : NULL;
}

static bool check_boundary_reserve(uint64_t more)
{
    bool valid = true;
    if (check_boundary.length + more > check_boundary.capacity)
    {
        uint64_t capacity = check_boundary.capacity != 0 ? check_boundary.capacity : 1u << 20;
        while (capacity < check_boundary.length + more)
        {
            capacity *= 2;
        }
        uint8_t* bytes = realloc(check_boundary.bytes, capacity);
        valid = bytes != NULL;
        if (valid)
        {
            check_boundary.bytes = bytes;
            check_boundary.capacity = capacity;
        }
    }
    return valid;
}

// `count` elements of `bytes` each appended at 8 bytes' alignment after their count; an absent column is a count of 0.
static void check_boundary_write(const void* data, uint64_t count, uint64_t element_bytes)
{
    const uint64_t size = data != NULL ? count * element_bytes : 0;
    const uint64_t padded = (size + 7) & ~UINT64_C(7);
    if (check_boundary_reserve(8 + padded))
    {
        const uint64_t stored = data != NULL ? count : 0;
        memcpy(check_boundary.bytes + check_boundary.length, &stored, 8);
        if (size != 0)
        {
            memcpy(check_boundary.bytes + check_boundary.length + 8, data, size);
        }
        memset(check_boundary.bytes + check_boundary.length + 8 + size, 0, padded - size);
        check_boundary.length += 8 + padded;
    }
}

// The column at `*cursor` (check_boundary_write's), which moves past it: its elements, or NULL for an absent one, and its
// count.
static void* check_boundary_read(uint64_t* cursor, uint64_t element_bytes, uint64_t* count)
{
    void* result = NULL;
    *count = 0;
    if (*cursor + 8 <= check_boundary.length)
    {
        uint64_t stored = 0;
        memcpy(&stored, check_boundary.bytes + *cursor, 8);
        const uint64_t size = stored * element_bytes;
        const uint64_t padded = (size + 7) & ~UINT64_C(7);
        if (*cursor + 8 + padded <= check_boundary.length)
        {
            result = stored != 0 ? check_boundary.bytes + *cursor + 8 : NULL;
            *count = stored;
            *cursor += 8 + padded;
        }
        else
        {
            *cursor = check_boundary.length + 1;
        }
    }
    else
    {
        *cursor = check_boundary.length + 1;
    }
    return result;
}

// The unit's recording's path in the directory: the source's last component and a hash of the whole path.
static void check_boundary_unit_path(const char* directory, const char* source)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (const char* c = source; *c != 0; c += 1)
    {
        hash = (hash ^ (uint8_t)*c) * UINT64_C(0x100000001b3);
    }
    const char* slash = strrchr(source, '/');
    snprintf(check_boundary.path, sizeof(check_boundary.path), "%s/%s.%016llx.boundary", directory, slash != NULL ? slash + 1 : source, (unsigned long long)hash);
}

static void check_boundary_unit_reset(void);

// The body of the unit that holds token `token` (an index in the unit's own parse), or BUSTER_CHECK_NONE.
static uint32_t check_boundary_range_of(uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = check_boundary.range_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (check_boundary.ranges[middle].opening <= token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low != 0 && token <= check_boundary.ranges[low - 1].closing ? low - 1 : UINT32_MAX;
}

// The unit's bodies' ranges, from its parse's roles and parents (`count` tokens): each OPEN_BODY to its closing brace.
static void check_boundary_ranges(const uint8_t* roles, const uint32_t* parents, uint32_t count, uint8_t open_body)
{
    uint32_t bodies = 0;
    for (uint32_t t = 0; t < count; t += 1)
    {
        if (roles[t] == open_body && parents[t] > t && parents[t] < count)
        {
            bodies += 1;
            t = parents[t];
        }
    }
    free(check_boundary.ranges);
    check_boundary.ranges = malloc(((uint64_t)bodies + 1) * sizeof(CheckBoundaryRange));
    check_boundary.range_count = 0;
    for (uint32_t t = 0; check_boundary.ranges != NULL && t < count; t += 1)
    {
        if (roles[t] == open_body && parents[t] > t && parents[t] < count)
        {
            check_boundary.ranges[check_boundary.range_count++] = (CheckBoundaryRange){ .opening = t, .closing = parents[t] };
            t = parents[t];
        }
    }
}

// A recording's use (the reader's unused analysis): of the declaration at `target` by the token `use`, both indexes in
// the unit's own parse. Kept where the use is in a body and the declaration outside it.
static void check_boundary_record_use(uint32_t use, uint32_t target, bool read)
{
    const uint32_t body = check_boundary_range_of(use);
    if (body != UINT32_MAX && (target < check_boundary.ranges[body].opening || target > check_boundary.ranges[body].closing))
    {
        if (check_boundary.met_use_count == check_boundary.met_use_capacity)
        {
            const uint32_t capacity = check_boundary.met_use_capacity != 0 ? check_boundary.met_use_capacity * 2 : 1u << 14;
            CheckBoundaryMetUse* grown = realloc(check_boundary.met_uses, (uint64_t)capacity * sizeof(CheckBoundaryMetUse));
            if (grown != NULL)
            {
                check_boundary.met_uses = grown;
                check_boundary.met_use_capacity = capacity;
            }
        }
        if (check_boundary.met_use_count < check_boundary.met_use_capacity)
        {
            check_boundary.met_uses[check_boundary.met_use_count++] = (CheckBoundaryMetUse){ .body = body, .target = target, .read = read ? 1u : 0u };
            check_boundary.met_uses_sorted = false;
        }
    }
}

// A recording's token whose body is refused though no diagnostic is at it.
static void check_boundary_refuse(uint32_t token)
{
    if (check_boundary.refused_token_count == check_boundary.refused_token_capacity)
    {
        const uint32_t capacity = check_boundary.refused_token_capacity != 0 ? check_boundary.refused_token_capacity * 2 : 64;
        uint32_t* grown = realloc(check_boundary.refused_tokens, (uint64_t)capacity * sizeof(uint32_t));
        if (grown != NULL)
        {
            check_boundary.refused_tokens = grown;
            check_boundary.refused_token_capacity = capacity;
        }
    }
    if (check_boundary.refused_token_count < check_boundary.refused_token_capacity)
    {
        check_boundary.refused_tokens[check_boundary.refused_token_count++] = token;
    }
}

static int check_boundary_met_use_order(const void* left, const void* right)
{
    const CheckBoundaryMetUse* a = left;
    const CheckBoundaryMetUse* b = right;
    return a->body != b->body ? (a->body < b->body ? -1 : 1) : a->target != b->target ? (a->target < b->target ? -1 : 1) : (a->read > b->read ? -1 : a->read < b->read);
}

// The recording's uses of the body at `opening`, each declaration once (read if any use of it reads): written into
// `uses`, of room for `capacity`; their count.
static uint32_t check_boundary_body_uses(uint32_t opening, CheckBoundaryUse* uses, uint32_t capacity)
{
    if (!check_boundary.met_uses_sorted && check_boundary.met_use_count != 0)
    {
        qsort(check_boundary.met_uses, check_boundary.met_use_count, sizeof(CheckBoundaryMetUse), check_boundary_met_use_order);
        check_boundary.met_uses_sorted = true;
    }
    const uint32_t body = check_boundary_range_of(opening);
    uint32_t low = 0;
    uint32_t high = check_boundary.met_use_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (check_boundary.met_uses[middle].body < body)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    // (A declaration's uses are together, a reading one first.)
    uint32_t count = 0;
    for (uint32_t k = low; body != UINT32_MAX && k < check_boundary.met_use_count && check_boundary.met_uses[k].body == body; k += 1)
    {
        const CheckBoundaryMetUse met = check_boundary.met_uses[k];
        if (k == low || check_boundary.met_uses[k - 1].target != met.target)
        {
            if (uses != NULL && count < capacity)
            {
                uses[count] = (CheckBoundaryUse){ .target = met.target, .read = met.read };
            }
            count += 1;
        }
    }
    return count;
}

// A unit's start: what the environment asks of it. A recording starts empty; a supplying compile reads the unit's
// recording, and supplies nothing where there is none.
static void check_boundary_begin(const char* source)
{
    free(check_boundary.bytes);
    free(check_boundary.bodies);
    free(check_boundary.ranges);
    free(check_boundary.met_uses);
    free(check_boundary.refused_tokens);
    check_boundary = (CheckBoundary){ 0 };
    check_boundary_unit_reset();
    const char* record = getenv("BUSTER_BOUNDARY_RECORD");
    const char* supply = getenv("BUSTER_BOUNDARY_SUPPLY");
    if (record != NULL && source != NULL)
    {
        check_boundary.mode = CHECK_BOUNDARY_RECORDING;
        check_boundary.directory = record;
        check_boundary_unit_path(record, source);
    }
    else if (supply != NULL && source != NULL)
    {
        const uint64_t start = __rdtsc();
        check_boundary.mode = CHECK_BOUNDARY_SUPPLYING;
        check_boundary.directory = supply;
        check_boundary_unit_path(supply, source);
        const char* select = getenv("BUSTER_BOUNDARY_SELECT");
        check_boundary.modulus = select != NULL && strtoul(select, NULL, 10) != 0 ? (uint32_t)strtoul(select, NULL, 10) : 1;
        check_boundary.residue = select != NULL && strchr(select, ':') != NULL ? (uint32_t)strtoul(strchr(select, ':') + 1, NULL, 10) : 0;
        const char* range = getenv("BUSTER_BOUNDARY_RANGE");
        check_boundary.first = range != NULL ? (uint32_t)strtoul(range, NULL, 10) : 0;
        check_boundary.end = range != NULL && strchr(range, ':') != NULL ? (uint32_t)strtoul(strchr(range, ':') + 1, NULL, 10) : UINT32_MAX;
        FILE* file = fopen(check_boundary.path, "rb");
        check_boundary.dropped = file == NULL ? "no recording" : NULL;
        if (file != NULL)
        {
            fseek(file, 0, SEEK_END);
            const long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            check_boundary.bytes = size > 0 ? malloc((uint64_t)size) : NULL;
            const bool read = check_boundary.bytes != NULL && fread(check_boundary.bytes, 1, (uint64_t)size, file) == (uint64_t)size;
            fclose(file);
            check_boundary.length = read ? (uint64_t)size : 0;
            check_boundary.capacity = check_boundary.length;
            check_boundary.dropped = read ? NULL : "unreadable recording";
        }
        check_boundary.load_ticks = __rdtsc() - start;
    }
}

// A recording's end: the unit's bodies written to its path. `bodies` is the recorded count.
static void check_boundary_record_finish(void);
static void check_boundary_record_position(uint32_t position);

// A supplying compile's report, in its directory beside the recording (never on the diagnostics' stream, which must stay
// the ordinary compile's).
static void check_boundary_report(const char* what)
{
    char path[800];
    snprintf(path, sizeof(path), "%s.%s", check_boundary.path, what);
    FILE* file = fopen(path, "w");
    if (file != NULL)
    {
        uint32_t refusals[CHECK_BOUNDARY_REFUSALS] = { 0 };
        uint64_t tokens[CHECK_BOUNDARY_REFUSALS] = { 0 };
        uint64_t entries[CHECK_BOUNDARY_REFUSALS] = { 0 };
        for (uint32_t b = 0; b < check_boundary.body_count; b += 1)
        {
            const CheckBoundaryBody* body = &check_boundary.bodies[b];
            const uint32_t refusal = body->refusal < CHECK_BOUNDARY_REFUSALS ? body->refusal : CHECK_BOUNDARY_SYMBOL;
            refusals[refusal] += 1;
            tokens[refusal] += body->closing - body->opening + 1;
            entries[refusal] += body->entries;
        }
        fprintf(file, "unit-tokens\t%u\nbodies\t%u\n", check_boundary.token_count, check_boundary.body_count);
        for (uint32_t r = 0; r < CHECK_BOUNDARY_REFUSALS; r += 1)
        {
            fprintf(file, "%s\t%u\t%llu\t%llu\n", check_boundary_refusal_names[r], refusals[r], (unsigned long long)tokens[r], (unsigned long long)entries[r]);
        }
        fprintf(file, "taken\t%u\t%llu\n", check_boundary.taken, (unsigned long long)check_boundary.taken_tokens);
        fprintf(file, "dropped\t%s\n", check_boundary.dropped != NULL ? check_boundary.dropped : "-");
        fprintf(file, "load-ticks\t%llu\n", (unsigned long long)check_boundary.load_ticks);
        fclose(file);
    }
}
