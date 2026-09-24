/* Canonical retirement command identity shared by the correctness preflight
 * and the paired-sample collector. Hash the exact argv, cwd and sorted,
 * explicit environment before either side can compare command identities. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_COMMAND_H
#define BUSTER_THROUGHPUT_RETIREMENT_COMMAND_H
#include <buster/lib/hash.h>
#include <string.h>

#define TP_RETIREMENT_COMMAND_ARGUMENTS 256u
#define TP_RETIREMENT_COMMAND_ENVIRONMENT 128u
#define TP_RETIREMENT_COMMAND_BYTES 65536u

static int tp_retirement_command_string(Sha256* hash, char const* value, unsigned* remaining)
{
    int ok = value && remaining && *remaining;
    if (ok) sha256_add(hash, "\"", 1);
    for (unsigned i = 0; ok && value[i]; ++i)
    {
        unsigned char c = (unsigned char)value[i];
        ok = *remaining > 0 && c >= 32 && c <= 126;
        if (ok)
        {
            --*remaining;
            if (c == '"' || c == '\\') sha256_add(hash, "\\", 1);
            sha256_add(hash, &c, 1);
        }
    }
    if (ok) ok = *remaining > 0;
    if (ok) { --*remaining; sha256_add(hash, "\"", 1); }
    return ok;
}

/* Canonical ASCII JSON with sorted keys argv, cwd, environment. Environment
 * entries must be sorted, unique NAME=value strings; no ambient variables
 * participate. Reject malformed or overlong commands before launch. */
static int tp_retirement_command_fields_hash(char* const* arguments, unsigned argument_count,
    char const* directory, char* const* environment, unsigned environment_count, char digest[65])
{
    int ok = digest && arguments && argument_count &&
        argument_count <= TP_RETIREMENT_COMMAND_ARGUMENTS &&
        environment && environment_count <= TP_RETIREMENT_COMMAND_ENVIRONMENT &&
        directory && directory[0] == '/' && arguments[argument_count] == NULL &&
        environment[environment_count] == NULL;
    unsigned remaining = TP_RETIREMENT_COMMAND_BYTES;
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, "{\"argv\":[", 9);
    for (unsigned i = 0; ok && i < argument_count; ++i)
    {
        if (i) sha256_add(&hash, ",", 1);
        ok = tp_retirement_command_string(&hash, arguments[i], &remaining);
    }
    sha256_add(&hash, "],\"cwd\":", 8);
    if (ok) ok = tp_retirement_command_string(&hash, directory, &remaining);
    sha256_add(&hash, ",\"environment\":[", 16);
    size_t previous_name = 0;
    for (unsigned i = 0; ok && i < environment_count; ++i)
    {
        char const* entry = environment[i];
        if (i) sha256_add(&hash, ",", 1);
        ok = tp_retirement_command_string(&hash, entry, &remaining);
        char const* equal = ok ? strchr(entry, '=') : NULL;
        size_t name = equal ? (size_t)(equal - entry) : 0;
        ok = ok && name;
        for (size_t j = 0; ok && j < name; ++j)
            ok = (entry[j] >= 'A' && entry[j] <= 'Z') || (entry[j] >= 'a' && entry[j] <= 'z') ||
                entry[j] == '_' || (j && entry[j] >= '0' && entry[j] <= '9');
        if (ok && i)
            ok = strcmp(environment[i - 1], entry) < 0 &&
                !(name == previous_name && !memcmp(environment[i - 1], entry, name));
        previous_name = name;
    }
    sha256_add(&hash, "]}", 2);
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}
#endif
