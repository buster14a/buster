// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's __builtin_available (#148): `__builtin_available(spec, ...)`, each spec a `*` or a platform's name and
// version. The parser reads the specs' tokens (c_parser.c's PARSE_MODE_AVAILABILITY_SPEC); this file gives the checks
// clang's parser makes of them past their kinds (ParseAvailabilitySpec, ParseVersionTuple and
// CheckAvailabilitySpecList), and clang's -Wunsupported-availability-guard (DiagnoseUnguardedAvailability). The call is a
// _Bool that is no constant expression; for an x86-64 Linux target, whose platform no spec names (its platform name is
// empty, the `*` spec's), clang's code is the constant 1. A rare builtin of another target's: scalar.
// Included by c_check_input.c, before c_check_builtin_input.c.

// The platforms clang 23.1.5 takes in a spec, by their canonical names (Attr.td's AvailabilityAttr: those
// getPrettyPlatformName names), and the other names canonicalizePlatformName maps to them.
static const char* const check_availability_platforms[] =
{
    "android", "fuchsia", "ios", "macos", "tvos", "watchos", "driverkit", "ios_app_extension", "macos_app_extension", "tvos_app_extension",
    "watchos_app_extension", "maccatalyst", "maccatalyst_app_extension", "xros", "xros_app_extension", "anyappleos", "swift", "shadermodel", "ohos",
};

#define BUSTER_CHECK_AVAILABILITY_ANY_APPLE 15u

typedef struct CheckAvailabilityAlias CheckAvailabilityAlias;
struct CheckAvailabilityAlias
{
    const char* given;
    uint8_t platform;
};

static const CheckAvailabilityAlias check_availability_aliases[] =
{
    { "iOS", 2 }, { "macOS", 3 }, { "macOSX", 3 }, { "macosx", 3 }, { "tvOS", 4 }, { "watchOS", 5 }, { "DriverKit", 6 },
    { "iOSApplicationExtension", 7 }, { "macOSApplicationExtension", 8 }, { "tvOSApplicationExtension", 9 },
    { "watchOSApplicationExtension", 10 }, { "macCatalyst", 11 }, { "macCatalystApplicationExtension", 12 }, { "visionOS", 13 },
    { "visionOSApplicationExtension", 14 }, { "visionos", 13 }, { "visionos_app_extension", 14 }, { "anyAppleOS", 15 }, { "ShaderModel", 17 },
};

_Static_assert(BUSTER_ARRAY_LENGTH(check_availability_platforms) == 19, "clang's 19 availability platforms");

static bool check_availability_equal(const uint8_t* bytes, uint32_t length, const char* text)
{
    const bool result = strlen(text) == length && memcmp(bytes, text, length) == 0;
    return result;
}

static bool check_availability_contains(const uint8_t* bytes, uint32_t length, const char* text)
{
    const uint32_t width = (uint32_t)strlen(text);
    bool result = false;
    for (uint32_t i = 0; !result && i + width <= length; i += 1)
    {
        result = memcmp(bytes + i, text, width) == 0;
    }
    return result;
}

// The platform a spec's name gives (an index of check_availability_platforms), or UINT32_MAX where clang refuses it
// ("unrecognized platform name"): a name canonicalizePlatformName maps to none of them, and one with "xros" or "xrOS" in
// it, which clang 23 refuses for visionOS's old name.
static uint32_t check_availability_platform(const uint8_t* bytes, uint32_t length)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; result == UINT32_MAX && i < BUSTER_ARRAY_LENGTH(check_availability_aliases); i += 1)
    {
        result = check_availability_equal(bytes, length, check_availability_aliases[i].given) ? check_availability_aliases[i].platform : UINT32_MAX;
    }
    for (uint32_t i = 0; result == UINT32_MAX && i < BUSTER_ARRAY_LENGTH(check_availability_platforms); i += 1)
    {
        result = check_availability_equal(bytes, length, check_availability_platforms[i]) ? i : UINT32_MAX;
    }
    if (check_availability_contains(bytes, length, "xros") || check_availability_contains(bytes, length, "xrOS"))
    {
        result = UINT32_MAX;
    }
    return result;
}

// A version's reading (ParseVersionTuple): a version, an error at the number ("expected a version of the form ..."), an
// error at the token after it ("version number must have non-zero ..."), or none of either, for a version whose
// components clang's VersionTuple keeps are all 0 (`0.0.0`), which clang refuses without a diagnostic.
#define BUSTER_CHECK_VERSION_VALID 0u
#define BUSTER_CHECK_VERSION_EXPECTED 1u
#define BUSTER_CHECK_VERSION_ZERO 2u
#define BUSTER_CHECK_VERSION_EMPTY 3u

typedef struct CheckAvailabilityVersion CheckAvailabilityVersion;
struct CheckAvailabilityVersion
{
    // The components, as VersionTuple keeps them: the major in 32 bits, the others in 31.
    uint32_t major;
    uint32_t minor;
    uint32_t subminor;
    uint8_t components;
    uint8_t status;
    // The separators differ (`1.2_3`): clang's -Wavailability warning at the number, before any error there.
    bool mixed;
};

static bool check_availability_separator(uint8_t c)
{
    return c == '.' || c == '_';
}

// The digits from `*at`, as clang reads them: an unsigned value that wraps.
static uint32_t check_availability_digits(const uint8_t* bytes, uint32_t length, uint32_t* at)
{
    uint32_t value = 0;
    while (*at < length && bytes[*at] >= '0' && bytes[*at] <= '9')
    {
        value = value * 10u + (uint32_t)(bytes[*at] - '0');
        *at += 1;
    }
    return value;
}

// A number's spelling (translation phases 1 and 2 applied) read as clang's ParseVersionTuple reads it.
static CheckAvailabilityVersion check_availability_version(const uint8_t* bytes, uint32_t length)
{
    CheckAvailabilityVersion result = { .status = BUSTER_CHECK_VERSION_EXPECTED };
    uint32_t at = 0;
    const uint32_t major = check_availability_digits(bytes, length, &at);
    const uint32_t after_major = at;
    const bool one = after_major != 0 && after_major == length;
    const bool split = after_major != 0 && after_major < length && check_availability_separator(bytes[after_major]) && after_major + 1 != length;
    if (one)
    {
        result = (CheckAvailabilityVersion){ .major = major, .components = 1, .status = major == 0 ? BUSTER_CHECK_VERSION_ZERO : BUSTER_CHECK_VERSION_VALID };
    }
    else if (split)
    {
        at = after_major + 1;
        const uint32_t minor = check_availability_digits(bytes, length, &at);
        const uint32_t after_minor = at;
        if (after_minor == length)
        {
            result = (CheckAvailabilityVersion){ .major = major, .minor = minor & 0x7FFFFFFFu, .components = 2,
                                                 .status = major == 0 && minor == 0 ? BUSTER_CHECK_VERSION_ZERO : BUSTER_CHECK_VERSION_VALID };
        }
        else if (check_availability_separator(bytes[after_minor]))
        {
            at = after_minor + 1;
            const uint32_t subminor = check_availability_digits(bytes, length, &at);
            result = (CheckAvailabilityVersion){ .major = major, .minor = minor & 0x7FFFFFFFu, .subminor = subminor & 0x7FFFFFFFu, .components = 3,
                                                 .status = at == length ? BUSTER_CHECK_VERSION_VALID : BUSTER_CHECK_VERSION_EXPECTED,
                                                 .mixed = bytes[after_major] != bytes[after_minor] };
        }
    }
    // An empty VersionTuple, which ParseAvailabilitySpec refuses with no diagnostic of its own.
    if (result.status == BUSTER_CHECK_VERSION_VALID && result.major == 0 && result.minor == 0 && result.subminor == 0)
    {
        result.status = BUSTER_CHECK_VERSION_EMPTY;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The availability attribute's platforms and versions (#1447, c_check_attribute_checks_input.c), which its diagnostics'
// renderers share.

// Each platform's name as clang's messages print it (AvailabilityAttr::getPrettyPlatformName), by its index of
// check_availability_platforms (anyAppleOS's is never printed: its attributes have none of these messages).
static const char* const check_availability_pretty[] =
{
    "Android", "Fuchsia", "iOS", "macOS", "tvOS", "watchOS", "DriverKit", "iOS (App Extension)", "macOS (App Extension)", "tvOS (App Extension)",
    "watchOS (App Extension)", "macCatalyst", "macCatalyst (App Extension)", "visionOS", "visionOS (App Extension)", "anyAppleOS", "Swift", "Shader Model",
    "OpenHarmony OS",
};

_Static_assert(BUSTER_ARRAY_LENGTH(check_availability_pretty) == BUSTER_ARRAY_LENGTH(check_availability_platforms), "a pretty name for each platform");

#define BUSTER_CHECK_AVAILABILITY_SWIFT 16u
#define BUSTER_CHECK_AVAILABILITY_FUCHSIA 1u

// The platform an availability attribute names (its name as written), as clang's parser and Sema take it: a name with
// xros or xrOS in it is kept as written (`*xros`, which the parser warns of), macosx and macosx_app_extension are macOS's,
// any other canonicalizePlatformName's; an index of check_availability_platforms, or UINT32_MAX for a platform of no
// pretty name (an unknown one).
static uint32_t check_availability_attribute_platform(const uint8_t* bytes, uint32_t length, bool* xros)
{
    *xros = check_availability_contains(bytes, length, "xros") || check_availability_contains(bytes, length, "xrOS");
    uint32_t result = !*xros && check_availability_equal(bytes, length, "macosx_app_extension") ? 8u : UINT32_MAX;
    for (uint32_t i = 0; !*xros && result == UINT32_MAX && i < BUSTER_ARRAY_LENGTH(check_availability_aliases); i += 1)
    {
        result = check_availability_equal(bytes, length, check_availability_aliases[i].given) ? check_availability_aliases[i].platform : UINT32_MAX;
    }
    for (uint32_t i = 0; result == UINT32_MAX && i < BUSTER_ARRAY_LENGTH(check_availability_platforms); i += 1)
    {
        result = check_availability_equal(bytes, length, check_availability_platforms[i]) ? i : UINT32_MAX;
    }
    return result;
}

// A parsed version as a VersionTuple (CheckVersionTuple).
static CheckVersionTuple check_version_tuple(CheckAvailabilityVersion version)
{
    const CheckVersionTuple result = { .major = version.major, .minor = version.minor, .subminor = version.subminor, .components = version.components };
    return result;
}

// VersionTuple's order of two versions (their missing components 0): negative, zero or positive.
static int check_version_compare(CheckVersionTuple a, CheckVersionTuple b)
{
    const int major = (a.major > b.major) - (a.major < b.major);
    const int minor = (a.minor > b.minor) - (a.minor < b.minor);
    const int subminor = (a.subminor > b.subminor) - (a.subminor < b.subminor);
    return major != 0 ? major : minor != 0 ? minor : subminor;
}

// Whether a version is VersionTuple's empty one (all its components 0).
static bool check_version_empty(CheckVersionTuple version)
{
    return version.major == 0 && version.minor == 0 && version.subminor == 0;
}

// A version as VersionTuple::getAsString prints it, its components as written, into `text` (`capacity` bytes).
static void check_version_print(CheckVersionTuple version, char* text, uint32_t capacity)
{
    snprintf(text, capacity, version.components <= 1 ? "%u" : version.components == 2 ? "%u.%u" : "%u.%u.%u", version.major, version.minor, version.subminor);
}

// The gap of versions a platform's OS skipped to 26 (llvm::Triple::isValidVersionForOS and getCanonicalVersionForOS, by
// AvailabilityAttr::getOSType): its last version before the gap, and the bump of a version in it in `*bump` (macOS's 16 and
// 10, iOS's, tvOS's and macCatalyst's 19 and 7, watchOS's 12 and 14, visionOS's 3 and 23); 0 for a platform of no gap.
static uint32_t check_availability_gap(uint32_t platform, uint32_t* bump)
{
    const bool macos = platform == 3 || platform == 8;
    const bool ios = platform == 2 || platform == 4 || platform == 7 || platform == 9 || platform == 11 || platform == 12;
    const bool watchos = platform == 5 || platform == 10;
    const bool xros = platform == 13 || platform == 14;
    *bump = macos ? 10u : ios ? 7u : watchos ? 14u : xros ? 23u : 0u;
    return macos ? 16u : ios ? 19u : watchos ? 12u : xros ? 3u : 0u;
}

// Whether `version` is valid for the platform's OS (out of its gap, check_availability_gap), and in `*canonical` the
// version clang's Sema takes for it (getCanonicalVersionForOS): macOS's 10.16 is 11.0, the version before a gap (16, 19,
// 12, 3) 26.0, and one in the gap its major bumped.
static bool check_availability_canonical(uint32_t platform, CheckVersionTuple version, CheckVersionTuple* canonical)
{
    uint32_t bump = 0;
    const uint32_t start = check_availability_gap(platform, &bump);
    const CheckVersionTuple first = { .major = start, .components = 1 };
    const CheckVersionTuple common = { .major = 26, .components = 1 };
    const CheckVersionTuple big_sur = { .major = 10, .minor = 16, .components = 2 };
    const bool valid = start == 0 || !(check_version_compare(version, first) > 0 && check_version_compare(version, common) < 0);
    *canonical = version;
    if (start == 16 && check_version_compare(version, big_sur) == 0)
    {
        *canonical = (CheckVersionTuple){ .major = 11, .components = 2 };
    }
    else if (start != 0 && check_version_compare(version, first) == 0)
    {
        *canonical = (CheckVersionTuple){ .major = 26, .components = 2 };
    }
    else if (!valid)
    {
        canonical->major += bump;
    }
    return valid;
}

// The version a spec's number gives: a numeric constant's spelling, where a number of another kind (a keyword the parser
// reads as one, `true` or `__func__`, or an #embed's data) is no version, as it is no numeric_constant of clang's.
static CheckAvailabilityVersion check_input_availability_number(CheckInputReader* reader, uint32_t token)
{
    const uint8_t kind = reader->pp->output.kinds[reader->parser->tokens.sources[token]];
    CheckAvailabilityVersion result = { .status = BUSTER_CHECK_VERSION_EXPECTED };
    const uint8_t* bytes = NULL;
    uint32_t length = 0;
    const uint64_t mark = reader->scratch->length;
    if ((kind == C_TOKEN_NUMERIC_CONSTANT || kind == BUSTER_PP_KIND_INTEGER) && check_input_spelling(reader, token, &bytes, &length))
    {
        result = check_availability_version(bytes, length);
    }
    reader->scratch->length = mark;
    return result;
}

// Whether call `close`'s specs are each valid, as clang's parser takes them, with clang's diagnostics where `report`
// asks for them: each spec's, in order, and then, where every spec is valid, the list's (a repeated `*`, a platform's
// second version, no `*`), which leave clang's expression valid, where a spec's error doesn't. `*silent` says whether a
// spec has an empty version, which clang refuses without a diagnostic.
static bool check_input_availability_specs(CheckInputReader* reader, uint32_t keyword, uint32_t close, bool report, bool* silent)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    bool empty = false;
    bool star = false;
    uint32_t seen = 0;
    uint32_t last = BUSTER_CHECK_NONE;
    uint32_t token = keyword + 2;
    // The specs: each a `*`, or a name and a number, as the parser read them.
    while (token < close)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && token + 1 < close)
        {
            const uint32_t number = token + 1;
            const CheckAvailabilityVersion version = check_input_availability_number(reader, number);
            const IdentId name = parser->tokens.names[token];
            const uint32_t platform = check_availability_platform(intern_bytes(reader->pp->table, name), intern_length(reader->pp->table, name));
            if (report && version.mixed)
            {
                const CheckWarningEvent event =
                {
                    .id = CHECK_WARNING_ID_AVAILABILITY,
                    .kind = CHECK_WARNING_EVENT_VERSION_SEPARATORS,
                    .token = number,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = number,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                check_input_warning_once(reader, &event);
            }
            const bool apple = version.status == BUSTER_CHECK_VERSION_VALID && platform == BUSTER_CHECK_AVAILABILITY_ANY_APPLE && version.major < 26;
            if (report && version.status == BUSTER_CHECK_VERSION_EXPECTED)
            {
                // Published past the number's warning (-Wavailability's), which clang gives first.
                check_input_report_argument(reader, number, CHECK_INPUT_AVAILABILITY_VERSION, 0);
            }
            else if (report && version.status == BUSTER_CHECK_VERSION_ZERO)
            {
                // clang's diagnostic is at its lookahead, past the number.
                check_input_report(reader, number + 1, CHECK_INPUT_AVAILABILITY_ZERO);
            }
            else if (report && version.status == BUSTER_CHECK_VERSION_VALID && platform == UINT32_MAX)
            {
                check_input_report(reader, token, CHECK_INPUT_AVAILABILITY_PLATFORM);
            }
            else if (report && apple)
            {
                check_input_report_argument(reader, number, CHECK_INPUT_AVAILABILITY_APPLE, 0);
            }
            empty |= version.status == BUSTER_CHECK_VERSION_EMPTY;
            const bool accepted = version.status == BUSTER_CHECK_VERSION_VALID && platform != UINT32_MAX && !apple;
            valid &= accepted;
            last = number;
            token = number + 2;
        }
        else
        {
            token += 2;
        }
    }
    // The list's checks, which clang makes only when every spec is valid. `seen` has a bit for each platform named.
    token = keyword + 2;
    while (valid && token < close)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_STAR)
        {
            if (star && report)
            {
                check_input_report(reader, token, CHECK_INPUT_AVAILABILITY_STAR);
            }
            star = true;
            token += 2;
        }
        else
        {
            const IdentId name = parser->tokens.names[token];
            const uint32_t platform = check_availability_platform(intern_bytes(reader->pp->table, name), intern_length(reader->pp->table, name));
            if ((seen & (UINT32_C(1) << platform)) != 0 && report)
            {
                check_input_report_argument(reader, token, CHECK_INPUT_AVAILABILITY_REPEATED, platform);
            }
            seen |= UINT32_C(1) << platform;
            token += 3;
        }
    }
    if (valid && !star && last != BUSTER_CHECK_NONE && report)
    {
        // Just past the last version (the driver places it there), where clang would insert ", *".
        check_input_report(reader, last, CHECK_INPUT_AVAILABILITY_WILDCARD);
    }
    *silent = empty;
    return valid;
}

// Whether call `close` (an availability check's `)`) is the condition of an if as written, through parentheses and `!`,
// as clang's extractAvailabilityExpr finds it: DiagnoseUnguardedAvailability checks nothing of such a condition.
static bool check_input_availability_guarded(const CheckInputReader* reader, uint32_t close)
{
    const Parser* parser = reader->parser;
    uint32_t node = parser->tree.parents[close];
    bool climbing = true;
    bool result = false;
    for (uint32_t depth = 0; climbing && depth < 256 && node < parser->tokens.count; depth += 1)
    {
        const uint8_t role = parser->tree.roles[node];
        if (role == PARSE_ROLE_CLOSE_GROUP || (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[node] == PARSE_KIND_EXCLAIM))
        {
            node = parser->tree.parents[node];
        }
        else
        {
            const uint32_t head = role == PARSE_ROLE_CLOSE_CONDITION ? reader->first_child[node] : BUSTER_CHECK_NONE;
            result = head < parser->tokens.count && parser->tokens.kinds[head] == PARSE_KIND_IF;
            climbing = false;
        }
    }
    return result;
}

// Whether `keyword` is in a case label's value (between its `case` and its `:`), which clang's
// DiagnoseUnguardedAvailability doesn't visit (TraverseCaseStmt): an ancestor's label whose value holds the keyword.
static bool check_input_availability_case(const CheckInputReader* reader, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    uint32_t node = parser->tree.parents[keyword];
    bool result = false;
    for (uint32_t depth = 0; !result && depth < 4096 && node < parser->tokens.count; depth += 1)
    {
        const uint32_t head = parser->tree.roles[node] == PARSE_ROLE_CLOSE_CASE ? reader->first_child[node] : BUSTER_CHECK_NONE;
        result = head < keyword && keyword < node;
        node = parser->tree.parents[node];
    }
    return result;
}

// -Wunsupported-availability-guard's warning at an availability check (`keyword` to its `)`, `close`) in a function's
// body, unless it is an if's condition or a case label's value, which clang gives once it has read the body; the walk
// and the constant evaluator (a typeof's operand, which the walk doesn't visit) each ask, once. A check whose specs
// aren't each valid is no expression of clang's, which no warning visits.
static void check_input_availability_warning(CheckInputReader* reader, uint32_t keyword, uint32_t close)
{
    const Parser* parser = reader->parser;
    const uint32_t body = reader->function_body;
    const uint32_t body_close = body < parser->tokens.count ? parser->tree.parents[body] : BUSTER_CHECK_NONE;
    bool silent = false;
    if (body_close < parser->tokens.count && keyword > body && close < body_close && !check_input_availability_guarded(reader, close) &&
        !check_input_availability_case(reader, keyword) && check_input_availability_specs(reader, keyword, close, false, &silent))
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_UNSUPPORTED_AVAILABILITY_GUARD,
            .kind = CHECK_WARNING_EVENT_UNGUARDED_AVAILABILITY,
            .token = keyword,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AFTER_DELIMITER,
            .completion = body_close,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

// An availability check at `keyword` (#148), as the walk reads it in source order: clang's diagnostics of its specs; the
// builtin, whose call takes no operand (its specs name nothing of the unit's); and in a function's body, unless it is
// an if's condition, -Wunsupported-availability-guard's warning, which clang gives once it has read the body.
static void check_input_availability_prepare(CheckInputReader* reader, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t open = keyword + 1;
    const uint32_t close = open < parser->tokens.count ? parser->tree.parents[open] : parser->tokens.count;
    const bool shaped = open < close && close < parser->tokens.count && parser->tree.roles[open] == PARSE_ROLE_OPEN_KEYWORD;
    bool silent = false;
    if (shaped)
    {
        check_input_availability_specs(reader, keyword, close, true, &silent);
        check_input_rare_claim(reader, &reader->builtins[close]);
        reader->builtins[close] = CBUILTIN_AVAILABLE;
        reader->ignored[keyword] = true;
        memset(reader->ignored + open + 1, 1, close - open - 1);
        check_input_availability_warning(reader, keyword, close);
    }
    else
    {
        check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
    }
}
