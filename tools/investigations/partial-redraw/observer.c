/* Separate translation unit, linked without LTO. Outside timed regions it
 * observes complete frame/list/output snapshots, keeping their writes live.
 * This diagnostic hash is not an equality or resource-identity certificate.
 */
#include <stddef.h>
#include <stdint.h>

void publish_snapshot(const void *pointer, size_t count)
{
    static volatile uint64_t publication_sink;
    const unsigned char *bytes = pointer;
    publication_sink += bytes[0] + bytes[count - 1];
}

uint64_t observe_bytes(const void *pointer, size_t count)
{
    const unsigned char *bytes = pointer;
    uint64_t result = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < count; i += 1)
    {
        result = (result ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return result;
}
