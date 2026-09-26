// Supplement to #1342. Research-only hosted control; never edits produced .i.
// Reuse the exact first-run process observer, generator and expected-layout rule.
#define main pp_pack_first_experiment_main
#include "pp-pack-law-20260925/probe.c"
#undef main

static bool supplementary_case(Spec spec, char const* purpose)
{
    Observation observation = evaluate(spec, purpose);
    char source[1200], gcc_plain[1200], gcc_raw[1200], path[1200], values[128], text[16384];
    bool ok = observation.ready && observation.violation;
    ok = make_path(source, sizeof(source), observation.directory, "source.c") && ok;
    ok = make_path(gcc_plain, sizeof(gcc_plain), observation.directory, "gcc-no-lines.i") && ok;
    ok = make_path(gcc_raw, sizeof(gcc_raw), observation.directory, "gcc-raw.i") && ok;
    char const* plain_arguments[] = {
        "gcc", "-m64", "-std=c17", "-nostdinc", "-g0", "-O0", "-fwrapv",
        "-fno-strict-aliasing", "-funsigned-char", "-E", "-P", source, "-o", gcc_plain, NULL,
    };
    if (ok) ok = run_command(observation.directory, "gcc-no-lines", plain_arguments, NULL, 0) && nonempty_file(gcc_plain);
    if (ok) ok = read_text(gcc_plain, text, sizeof(text)) && strstr(text, "#pragma pack") != NULL;
    if (ok) ok = read_text(observation.split, text, sizeof(text)) && strstr(text, "#pragma") == NULL;
    unsigned consumers_passed = 0;
    for (unsigned producer = 0; producer < 3 && ok; ++producer)
    {
        char label[80];
        snprintf(label, sizeof(label), "gcc-no-lines-consumer-%u", producer);
        ok = compile_run(spec, observation.directory, label, producer, gcc_plain, true, values, sizeof(values));
        ok = ok && strcmp(values, observation.expected) == 0;
        if (ok) ++consumers_passed;
    }
    unsigned loss_consumers_passed = 0;
    for (unsigned producer = 1; producer < 3 && ok; ++producer)
    {
        char label[80];
        snprintf(label, sizeof(label), "buster-text-consumer-%u", producer);
        ok = compile_run(spec, observation.directory, label, producer, observation.split, true, values, sizeof(values));
        ok = ok && strcmp(values, observation.actual) == 0;
        if (ok) ++loss_consumers_passed;
    }
    // Captured stdout is consumed verbatim through the explicit input phase.
    char const* stdout_arguments[] = {
        ide, "cc", "-target", "x86_64-unknown-linux", "-std=c17", "-nostdinc", "-g0", "-O0",
        "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-E", source, NULL,
    };
    bool stdout_passed = false;
    if (ok) ok = run_command(observation.directory, "buster-preprocess-stdout", stdout_arguments, NULL, 0);
    if (ok) ok = make_path(path, sizeof(path), observation.directory, "buster-preprocess-stdout.stdout") && nonempty_file(path);
    if (ok)
    {
        Spec explicit_input = spec;
        explicit_input.explicit_phase = 1;
        ok = compile_run(explicit_input, observation.directory, "buster-stdout-consumer", 0, path, true, values, sizeof(values));
        stdout_passed = ok && strcmp(values, observation.actual) == 0;
        ok = ok && stdout_passed;
    }
    // Raw GCC output remains unmodified and rejected. Check the exact cause,
    // not merely any failure. This is a rejected transport precondition, not
    // a semantically invalid C program and not an accepted packing observation.
    bool marker_rejection = false;
    if (ok) ok = compile_file(spec, observation.directory, "gcc-raw", 2, true, source, gcc_raw, false);
    if (ok) ok = read_text(gcc_raw, text, sizeof(text)) && strncmp(text, "# 0 ", 4) == 0 && strstr(text, "#pragma pack") != NULL;
    if (ok)
    {
        char binary[1200], error[1200], record[1200];
        ok = make_path(binary, sizeof(binary), observation.directory, "rejected-gcc-marker.exe");
        ok = make_path(error, sizeof(error), observation.directory, "rejected-gcc-marker.stderr") && ok;
        ok = make_path(record, sizeof(record), observation.directory, "rejected-gcc-marker.command") && ok;
        bool accepted = ok && compile_file(spec, observation.directory, "rejected-gcc-marker", 0, false, gcc_raw, binary, true);
        marker_rejection = ok && !accepted && read_text(error, text, sizeof(text)) &&
                           strstr(text, "expected '#line' followed by a positive line number and optional file name") != NULL;
        marker_rejection = marker_rejection && read_text(record, text, sizeof(text)) &&
                           strstr(text, "\nexit=1\nsignal=0\ntimeout=0\n") != NULL;
        ok = ok && marker_rejection;
    }
    printf("SUPPLEMENT purpose=%s directory=%s gcc_no_lines_consumers=%u buster_text_consumers=%u stdout_loss=%u raw_marker_rejection=%u controls_pass=%u\n",
           purpose, observation.directory, consumers_passed, loss_consumers_passed, stdout_passed, marker_rejection, ok);
    return ok;
}

int main(int argc, char** argv)
{
    int result = 2;
    if (argc == 3 && strlen(argv[1]) < 700 && strlen(argv[2]) < 700)
    {
        ide = argv[1];
        root = argv[2];
        if (mkdir(root, 0700) == 0)
        {
            Spec initial = {.pack = 2, .prefix = 3, .tail = 2, .noise = 3, .header = 1, .macro = 1,
                            .extra = 1, .passes = 2, .explicit_phase = 1, .jobs = 2, .natural_sentinel = 1, .paired = 1};
            Spec reduced = {.pack = 1, .prefix = 1, .passes = 1, .jobs = 1};
            bool first = supplementary_case(initial, "initial-context");
            bool second = supplementary_case(reduced, "reduced-context");
            bool ok = first && second && setup_errors == 0;
            printf("SUPPLEMENT_SUMMARY contexts=2 controls_pass=%u primary_packing_loss=%u setup_errors=%u\n", ok, violations, setup_errors);
            // Green here means only that the supplementary controls behaved as
            // declared. The original metamorphic-law workflow remains red.
            result = ok ? 0 : 2;
        }
        else perror("fresh output directory required");
    }
    else fprintf(stderr, "usage: controls ABSOLUTE_IDE NEW_OUTPUT_DIRECTORY\n");
    return result;
}
