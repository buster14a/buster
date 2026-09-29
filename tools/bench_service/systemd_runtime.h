/* Bounded unit RuntimeMax contract (#881-C), shared by the systemd broker and
 * the Linux worker so the value a coordinator derives, the property the broker
 * installs and the readback both sides compare cannot drift apart.
 *
 * The admitted smoke recipe keeps its fixed one-hour limit for the outer unit
 * and every nested stage. The retirement recipe (A1) instead carries one
 * whole-second limit derived by the coordinator from its authenticated
 * campaign budget ceiling (worker_linux.c, bq_worker_retirement_runtime). The
 * coordinator passes it in the typed broker request for the outer unit; the
 * broker rejects anything outside [BQ_SYSTEMD_RETIREMENT_RUNTIME_MIN_USEC,
 * BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC] or not a whole number of seconds,
 * and each nested retirement stage inherits the outer unit's effective value
 * read back from the manager, never a caller-chosen one.
 *
 * systemctl show renders RuntimeMaxUSec as a timespan ("1h", "2h 24min",
 * "1d 3h 5s"). bq_systemd_timespan_parse accepts exactly plain microseconds or
 * nonzero whole-second d/h/min/s components in descending order, so a readback is
 * compared numerically instead of by spelling. The broker has no Buster
 * dependency, so this header uses only the C standard library.
 *
 * Map: BQ_SYSTEMD_SMOKE_RUNTIME_USEC, BQ_SYSTEMD_RETIREMENT_RUNTIME_MIN_USEC,
 * BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC, BQ_SYSTEMD_RELAY_ALLOWANCE_MILLISECONDS,
 * BQ_SYSTEMD_RETIREMENT_OUTER_VERB, BQ_SYSTEMD_RETIREMENT_RECIPE,
 * bq_systemd_retirement_runtime_valid, bq_systemd_relay_milliseconds,
 * bq_systemd_timespan_parse.
 */
#ifndef BUSTER_BENCH_SYSTEMD_RUNTIME_H
#define BUSTER_BENCH_SYSTEMD_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BQ_SYSTEMD_USEC_PER_SECOND UINT64_C(1000000)
/* The admitted smoke recipe's unit limit, unchanged by this contract. */
#define BQ_SYSTEMD_SMOKE_RUNTIME_USEC (UINT64_C(3600) * BQ_SYSTEMD_USEC_PER_SECOND)
/* A retirement limit is a whole number of seconds in this closed range. The
 * minimum refuses a degenerate record; the maximum (three days) is a fixed
 * sanity ceiling above the A1 estimate (about 2.4 h at 60 pairs, rising with
 * the admitted pair count), not a budget. A reviewed ceiling above it is
 * refused until this constant is itself reviewed and raised. */
#define BQ_SYSTEMD_RETIREMENT_RUNTIME_MIN_USEC (UINT64_C(60) * BQ_SYSTEMD_USEC_PER_SECOND)
#define BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC (UINT64_C(72) * 3600 * BQ_SYSTEMD_USEC_PER_SECOND)
/* The one bound every consumer of the retirement execution deadline accepts
 * as its furthest distance from now: the recipe entry (build.c), the oracle
 * adapter and the reference producer. It equals the longest unit limit, so a
 * deadline the coordinator derives is never refused downstream as too far. */
#define BQ_SYSTEMD_RETIREMENT_DEADLINE_MAX_NS (BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC * UINT64_C(1000))
/* The broker relays systemd-run --wait for the unit's limit plus this fixed
 * stop and reporting allowance (the smoke relay is 3,700,000 ms). */
#define BQ_SYSTEMD_RELAY_ALLOWANCE_MILLISECONDS UINT64_C(100000)
/* Typed broker CLI verb for the retirement outer unit, whose trailing
 * argument is the limit in microseconds; start-outer stays the smoke form. */
#define BQ_SYSTEMD_RETIREMENT_OUTER_VERB "start-retirement-outer"
/* The workspace-relative directory holding each unit's lease-keeper socket
 * (worker_linux.c, bq_worker_lease_keeper_path). Every stage unit has it in
 * InaccessiblePaths, so no stage can reach a keeper; the outer unit, whose
 * keeper creates it before any stage starts, keeps access. */
#define BQ_SYSTEMD_LEASE_RETURN_DIRECTORY "results/.lease-return"
/* The recipe name the broker writes into the retirement outer command. */
#define BQ_SYSTEMD_RETIREMENT_RECIPE "native-retirement-performance-v1"

static inline bool bq_systemd_retirement_runtime_valid(uint64_t usec)
{
    bool valid = usec >= BQ_SYSTEMD_RETIREMENT_RUNTIME_MIN_USEC &&
                 usec <= BQ_SYSTEMD_RETIREMENT_RUNTIME_MAX_USEC &&
                 usec % BQ_SYSTEMD_USEC_PER_SECOND == 0;
    return valid;
}

/* The bounded relay wait for a unit with this limit; zero for a limit that is
 * neither the smoke value nor a valid retirement value. */
static inline uint64_t bq_systemd_relay_milliseconds(uint64_t usec)
{
    bool known = usec == BQ_SYSTEMD_SMOKE_RUNTIME_USEC || bq_systemd_retirement_runtime_valid(usec);
    uint64_t result = known ? usec / 1000 + BQ_SYSTEMD_RELAY_ALLOWANCE_MILLISECONDS : 0;
    return result;
}

/* Parse `length` bytes of a manager timespan into microseconds: either plain
 * decimal microseconds, or single-space-separated `<n>d`, `<n>h`, `<n>min`,
 * `<n>s` components, each at most once and in that order. Fractions, weeks
 * and larger units, "infinity", leading zeros and overflow are rejected. */
static inline bool bq_systemd_timespan_parse(char const* text, size_t length, uint64_t* usec)
{
    static char const* const units[] = {"d", "h", "min", "s"};
    static uint64_t const scales[] = {UINT64_C(86400) * BQ_SYSTEMD_USEC_PER_SECOND,
                                      UINT64_C(3600) * BQ_SYSTEMD_USEC_PER_SECOND,
                                      UINT64_C(60) * BQ_SYSTEMD_USEC_PER_SECOND, BQ_SYSTEMD_USEC_PER_SECOND};
    uint64_t total = 0;
    size_t offset = 0;
    unsigned next_unit = 0;
    unsigned components = 0;
    bool plain = length > 0 && text != NULL;
    for (size_t index = 0; plain && index < length; index += 1) plain = text[index] >= '0' && text[index] <= '9';
    bool ok = text != NULL && usec != NULL && length > 0 && length <= 64 && (text[0] != '0' || length == 1);
    while (ok && !plain && offset < length)
    {
        uint64_t value = 0;
        size_t digits = 0;
        while (ok && offset + digits < length && text[offset + digits] >= '0' && text[offset + digits] <= '9')
        {
            unsigned digit = (unsigned)(text[offset + digits] - '0');
            ok = value <= (UINT64_MAX - digit) / 10;
            if (ok) value = value * 10 + digit;
            digits += 1;
        }
        /* systemd omits zero components, so a zero one is not its text. */
        ok = ok && digits > 0 && text[offset] != '0';
        size_t unit_start = offset + digits;
        size_t unit_end = unit_start;
        while (unit_end < length && text[unit_end] != ' ') unit_end += 1;
        unsigned found = (unsigned)(sizeof(units) / sizeof(units[0]));
        for (unsigned unit = next_unit; ok && found == sizeof(units) / sizeof(units[0]) &&
                                        unit < sizeof(units) / sizeof(units[0]); unit += 1)
        {
            size_t unit_length = strlen(units[unit]);
            if (unit_end - unit_start == unit_length && !memcmp(text + unit_start, units[unit], unit_length))
                found = unit;
        }
        ok = ok && found < sizeof(units) / sizeof(units[0]) && value <= UINT64_MAX / scales[found] &&
             total <= UINT64_MAX - value * scales[found];
        if (ok)
        {
            total += value * scales[found];
            next_unit = found + 1;
            components += 1;
            /* Exactly one space separates components; none may trail. */
            ok = unit_end == length || (unit_end + 1 < length && text[unit_end] == ' ');
            offset = unit_end == length ? length : unit_end + 1;
        }
    }
    for (size_t index = 0; ok && plain && index < length; index += 1)
    {
        unsigned digit = (unsigned)(text[index] - '0');
        ok = total <= (UINT64_MAX - digit) / 10;
        if (ok) total = total * 10 + digit;
    }
    ok = ok && (plain || components > 0);
    if (usec) *usec = ok ? total : 0;
    return ok;
}

#endif
