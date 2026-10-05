/* relax_model ALG N R K: model of AArch64 branch relaxation metadata work.
   N code bytes, R machine rows (u32 offsets), K far conditional fixups at the
   front of the function, each needing one 32-byte insertion (tier 2, as
   observed in the switch family).  ALG 0 = restart-after-each-expansion with
   eager full shift (current machine_a64_relax_branches shape); ALG 1 =
   collect a whole pass of expansions, then one merge sweep over bytes, rows
   and fixups (prefix-sum delta). Prints final size and checksum so both must
   agree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef struct { uint32_t patch; uint32_t target; uint8_t expanded; } Fix;
static uint8_t* bytes; static uint32_t count, cap; static uint32_t* rows; static Fix* fx;
static uint32_t R, K;
static int far_(Fix* f) { int64_t d = (int64_t)f->target - f->patch; return !f->expanded && (d > 1048572 || d < -1048576); }
static void shift(uint32_t at, uint32_t n)
{
    memmove(bytes + at + n, bytes + at, count - at); memset(bytes + at, 0, n); count += n;
    for (uint32_t i = 0; i < R; i++) if (rows[i] >= at) rows[i] += n;
    for (uint32_t i = 0; i < K; i++) { if (fx[i].patch >= at) fx[i].patch += n; if (fx[i].target >= at) fx[i].target += n; }
}
int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    int alg = atoi(argv[1]); count = (uint32_t)atol(argv[2]); R = (uint32_t)atol(argv[3]); K = (uint32_t)atol(argv[4]);
    cap = count + K * 32u + 64; bytes = calloc(cap, 1); rows = malloc(sizeof(*rows) * R); fx = calloc(K, sizeof(*fx));
    for (uint32_t i = 0; i < count; i++) bytes[i] = (uint8_t)(i * 31u);
    for (uint32_t i = 0; i < R; i++) rows[i] = (uint32_t)((uint64_t)i * count / R) & ~3u;
    for (uint32_t i = 0; i < K; i++) { fx[i].patch = 64 + i * 16u; fx[i].target = count - 64; }
    if (alg == 0)
    {
        for (;;) { int changed = 0; for (uint32_t i = 0; i < K && !changed; i++) if (far_(&fx[i])) { shift(fx[i].patch + 4, 32); fx[i].expanded = 2; changed = 1; } if (!changed) break; }
    }
    else
    {
        uint32_t* at = malloc(sizeof(*at) * (K ? K : 1));
        for (;;)
        {
            uint32_t n = 0;
            for (uint32_t i = 0; i < K; i++) if (far_(&fx[i])) { at[n++] = fx[i].patch + 4; fx[i].expanded = 2; }
            if (!n) break;
            /* insertion points are ascending (fixups are in code order) */
            uint32_t grow = n * 32u, src = count, dst = count + grow;
            for (uint32_t j = n; j-- > 0;) { uint32_t len = src - at[j]; dst -= len; src -= len; memmove(bytes + dst, bytes + src, len); dst -= 32; memset(bytes + dst, 0, 32); }
            count += grow;
            uint32_t j = 0; for (uint32_t i = 0; i < R; i++) { while (j < n && at[j] <= rows[i]) j++; rows[i] += j * 32u; }
            j = 0; for (uint32_t i = 0; i < K; i++) { while (j < n && at[j] <= fx[i].patch) j++; fx[i].patch += j * 32u; }
            for (uint32_t i = 0; i < K; i++) { uint32_t lo = 0, hi = n; while (lo < hi) { uint32_t mid = (lo + hi) / 2; if (at[mid] <= fx[i].target) lo = mid + 1; else hi = mid; } fx[i].target += lo * 32u; }
        }
    }
    uint64_t h = 1469598103934665603ull; for (uint32_t i = 0; i < count; i++) h = (h ^ bytes[i]) * 1099511628211ull;
    for (uint32_t i = 0; i < R; i++) h = (h ^ rows[i]) * 1099511628211ull;
    for (uint32_t i = 0; i < K; i++) h = (h ^ fx[i].patch ^ ((uint64_t)fx[i].target << 32)) * 1099511628211ull;
    printf("alg=%d N=%u R=%u K=%u final=%u hash=%016llx\n", alg, (unsigned)atol(argv[2]), R, K, count, (unsigned long long)h);
    return 0;
}
