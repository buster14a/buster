/* Append-only decisions in one trusted evidence directory per series.
 * The directory lock covers replay and publication. A hard link installs
 * a flushed event without replacing a previous attempt after a crash.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <dirent.h>

typedef struct PairHistory PairHistory;
struct PairHistory
{
    size_t count;
    size_t prior;
    bool adverse;
    size_t duplicate;
    char previous[65];
    char series[65];
    char anchor[65];
    char latest[65];
};

static bool paired_saved_hash(const PerfJson *json, const char *key, char hash[65])
{
    PerfBytes value = paired_field(json, 0, key);
    uint32_t node = perf_json_find(json, 0, key);
    bool valid = node < json->node_count && json->nodes[node].kind == PERF_JSON_STRING && (value.size == 0 || paired_hex(value, 64));
    if (valid)
    {
        memcpy(hash, value.data, value.size);
        hash[value.size] = 0;
    }
    return valid;
}

static bool paired_history_read(const char *directory, int fd, const char *candidate, const char *request, PairHistory *history)
{
    DIR *entries = fdopendir(dup(fd));
    bool valid = entries != NULL;
    size_t count = 0;
    struct dirent *entry;
    errno = 0;
    while (valid && (entry = readdir(entries)) != NULL)
    {
        if (entry->d_name[0] != '.')
        {
            ++count;
        }
    }
    valid = valid && errno == 0;
    if (entries)
    {
        closedir(entries);
    }
    for (size_t i = 1; valid && i <= count; ++i)
    {
        char path[4096];
        int length = snprintf(path, sizeof(path), "%s/%08zu.json", directory, i);
        PerfJson event = {0};
        char digest[65];
        valid = length > 0 && (size_t)length < sizeof(path) && paired_load_json(path, &event, digest) &&
                perf_json_is(&event, perf_json_find(&event, 0, "previous_sha256"), history->previous);
        size_t seq = 0;
        valid = valid && paired_count(&event, 0, "sequence", i, &seq) && seq == i &&
                paired_saved_hash(&event, "series", history->series) && paired_saved_hash(&event, "anchor", history->anchor) &&
                paired_saved_hash(&event, "latest", history->latest);
        if (valid)
        {
            if (perf_json_is(&event, perf_json_find(&event, 0, "request_sha256"), request))
            {
                history->duplicate = i;
            }
            memcpy(history->previous, digest, sizeof(digest));
            if (perf_json_is(&event, perf_json_find(&event, 0, "candidate_source"), candidate))
            {
                history->prior = i;
                history->adverse |= perf_json_is(&event, perf_json_find(&event, 0, "adverse"), "true");
            }
        }
        perf_json_free(&event);
    }
    history->count = count;
    return valid;
}

static bool paired_record(const PairPlan *p, const char *directory, const char *candidate, const char *latest, const char *anchor)
{
    PairReport a = {0};
    PairReport b = {0};
    char *reports = NULL;
    size_t size = 0;
    FILE *buffer = open_memstream(&reports, &size);
    bool valid = buffer && paired_hex(perf_literal(candidate), 40);
    if (valid)
    {
        paired_report(buffer, p, latest, &a);
        fputs(",", buffer);
        paired_report(buffer, p, anchor, &b);
        valid = !ferror(buffer);
    }
    if (buffer)
    {
        valid = fclose(buffer) == 0 && valid;
    }
    char request[65];
    PerfSha256 hash = perf_sha256_start();
    valid = valid && perf_sha256_add(&hash, perf_literal(candidate)) && perf_sha256_add(&hash, (PerfBytes){(unsigned char *)reports, size});
    perf_sha256_finish(&hash, request);
    int fd = valid ? open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    PairHistory history = {0};
    valid = fd >= 0 && flock(fd, LOCK_EX) == 0 && paired_history_read(directory, fd, candidate, request, &history);
    char temp[4096];
    int length = snprintf(temp, sizeof(temp), "%s/.attempt-XXXXXX", directory);
    bool append = valid && history.duplicate == 0;
    int output = append && length > 0 && (size_t)length < sizeof(temp) ? mkstemp(temp) : -1;
    FILE *file = output >= 0 ? fdopen(output, "w") : NULL;
    valid = valid && (!append || file != NULL);
    char name[64];
    snprintf(name, sizeof(name), "%08zu.json", append ? history.count + 1 : history.duplicate);
    if (valid && append)
    {
        fprintf(file, "{\"sequence\":%zu,\"previous_sha256\":\"%s\",\"request_sha256\":\"%s\",\"candidate_source\":\"%s\",\"prior_attempt\":%zu,\"comparisons\":[",
                history.count + 1, history.previous, request, candidate, history.prior);
        fwrite(reports, 1, size, file);
        bool eligible = a.promotable && b.promotable && strcmp(a.sources[1], candidate) == 0 &&
                        strcmp(a.series, b.series) == 0 && strcmp(a.builds[1], b.builds[1]) == 0;
        if (history.anchor[0] == 0)
        {
            // Seed a new series only with a measured self-comparison.
            eligible = eligible && strcmp(a.builds[1], a.builds[0]) == 0 && strcmp(b.builds[1], b.builds[0]) == 0;
        }
        else
        {
            eligible = eligible && strcmp(a.series, history.series) == 0 && strcmp(a.builds[0], history.latest) == 0 && strcmp(b.builds[0], history.anchor) == 0;
        }
        bool promoted = eligible && !history.adverse;
        if (promoted)
        {
            if (history.anchor[0] == 0)
            {
                memcpy(history.anchor, a.builds[1], sizeof(history.anchor));
                memcpy(history.series, a.series, sizeof(history.series));
            }
            memcpy(history.latest, a.builds[1], sizeof(history.latest));
        }
        const char *decision = promoted ? "accepted" : !eligible ? "coverage, calibration, candidate or reference mismatch" : "prior adverse attempt";
        fprintf(file, "],\"decision\":\"%s\",\"adverse\":%s,\"promoted\":%s,\"series\":\"%s\",\"anchor\":\"%s\",\"latest\":\"%s\"}\n",
                decision, eligible ? "false" : "true", promoted ? "true" : "false", history.series, history.anchor, history.latest);
        valid = !ferror(file) && fflush(file) == 0 && fsync(output) == 0;
    }
    if (file)
    {
        valid = fclose(file) == 0 && valid;
    }
    else if (output >= 0)
    {
        close(output);
    }
    if (valid && append)
    {
        PerfJson check = {0};
        valid = paired_load_json(temp, &check, NULL);
        perf_json_free(&check);
    }
    valid = valid && (!append || (linkat(AT_FDCWD, temp, fd, name, 0) == 0 && fsync(fd) == 0));
    perf_dataset_free(&a.dataset);
    perf_dataset_free(&b.dataset);
    free(reports);
    if (output >= 0)
    {
        unlink(temp);
    }
    if (fd >= 0)
    {
        close(fd);
    }
    if (valid)
    {
        printf("%s/%s\n", directory, name);
    }
    return valid;
}
