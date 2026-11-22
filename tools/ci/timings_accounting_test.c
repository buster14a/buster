// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Private fixtures for timings_accounting.c, included only by its owning tool.

static void accounting_expect(bool result, const char* name, unsigned int* checks, unsigned int* failures)
{
    *checks += 1;
    if (!result)
    {
        *failures += 1;
        fprintf(stderr, "accounting fixture failed: %s\n", name);
    }
}

static bool accounting_fixture(Accounting* accounting, const char* source)
{
    const size_t length = strlen(source);
    size_t offset = 0;
    bool valid = accounting_allocated(accounting);
    while (offset < length && valid)
    {
        size_t end = offset;
        while (end < length && source[end] != '\n')
        {
            end += 1;
        }
        valid = end < length && end + 1 - offset < BUSTER_ACCOUNTING_LINE;
        if (valid)
        {
            char line[BUSTER_ACCOUNTING_LINE];
            memcpy(line, source + offset, end + 1 - offset);
            line[end + 1 - offset] = 0;
            valid = accounting_add(accounting, line);
            offset = end + 1;
        }
    }
    return valid;
}

static unsigned int accounting_test(unsigned int* checks)
{
    unsigned int failures = 0;
    Accounting* accounting = accounting_create();
    const char* facts =
        "meta\t1200\t1000\t-\t-\n"
        "run\t1\t1\t9700x.yml\tpr\tbefore\t1111111111111111111111111111111111111111\tfeature\tcompleted\tfailure\t900\t905\t9999\tna\t1\n"
        "job\t11\tPR without native endpoint\thosted\tubuntu-26.04\tCPU A\tcompleted\tfailure\t900\t910\t1010\t1\n"
        "step\t1\tInstall tools\tcompleted\tsuccess\t910\t930\n"
        "step\t2\tCompare compilers\tcompleted\tsuccess\t930\t950\n"
        "step\t3\tTime backend\tcompleted\tsuccess\t945\t970\n"
        "step\t4\tRun correctness tests\tcompleted\tfailure\t970\t995\n"
        "step\t5\tUpload diagnostics\tcompleted\tsuccess\t995\t1000\n"
        "step\t6\tComplete job\tcompleted\tsuccess\t1000\t1005\n"
        "step\t7\tBenchmark skipped\tcompleted\tskipped\t900\t1199\n"
        "step\t8\tMeasure missing start\tcompleted\tfailure\t-\t1000\n"
        "job\t12\tSkipped hardware\tnone\tself-hosted\t-\tcompleted\tskipped\t900\t1010\t1009\t1\n"
        "run\t1\t2\t9700x.yml\tpr\tbefore\t1111111111111111111111111111111111111111\tfeature\tcompleted\tsuccess\t900\t1100\t99999\tna\t1\n"
        "job\t13\tRetry correctness\thosted\tubuntu-26.04\tCPU B\tcompleted\tsuccess\t1100\t1110\t1150\t1\n"
        "step\t1\tBuild compiler\tcompleted\tsuccess\t1110\t1120\n"
        "step\t2\tTest compiler\tcompleted\tsuccess\t1120\t1150\n"
        "run\t2\t1\t9700x.yml\tqueue\tafter\t2222222222222222222222222222222222222222\tgh-readonly-queue/main/pr-2-a\tcompleted\tsuccess\t1050\t1060\t99999\tlive\t1\n"
        "job\t21\tCandidate\thosted\tubuntu-26.04\tCPU A\tcompleted\tsuccess\t1050\t1070\t1170\t1\n"
        "step\t1\tBuild compiler\tcompleted\tsuccess\t1080\t1090\n"
        "step\t2\tTest candidate\tcompleted\tsuccess\t1090\t1110\n"
        "run\t3\t1\t9700x.yml\tqueue\tafter\t3333333333333333333333333333333333333333\tgh-readonly-queue/main/pr-3-a\tcompleted\tfailure\t1055\t1060\t99999\tinvalidated\t1\n"
        "job\t31\tHardware lock fixture\tself-hosted\tself-hosted,buster-9700x\tRyzen\tcompleted\tfailure\t1055\t1060\t1160\t1\n"
        "step\t1\tSet up runner\tcompleted\tsuccess\t1060\t1080\n"
        "step\t2\tVerify benchmark host\tcompleted\tsuccess\t1080\t1090\n"
        "step\t3\tAttribute performance\tcompleted\tfailure\t1090\t1160\n"
        "lock\t1060\t1070\t1160\n"
        "run\t4\t1\tci-timings.yml\treporting\tafter\t4444444444444444444444444444444444444444\tmain\tin_progress\t-\t1100\t1140\t1199\tna\t1\n"
        "job\t41\tPartial report\thosted\tubuntu-26.04\t-\tin_progress\t-\t1140\t1150\t-\t1\n"
        "step\t1\tTime reporting\tin_progress\t-\t1150\t-\n"
        "run\t5\t1\tbuild.yml\tmain\tbefore\t5555555555555555555555555555555555555555\tmain\tcompleted\tfailure\t950\t960\t99999\tna\t1\n"
        "job\t51\tOnly step bounds\thosted\tubuntu-26.04\t-\tcompleted\tfailure\t960\t-\t1190\t1\n"
        "step\t1\tTest compiler\tcompleted\tsuccess\t1120\t1150\n"
        "step\t2\tMeasure backend\tcompleted\tfailure\t1160\t1190\n"
        "run\t6\t3\tbuild.yml\tpr\tafter\t6666666666666666666666666666666666666666\tfeature-6\tcompleted\tcancelled\t1050\t1160\t99999\tsuperseded-possible\t1\n"
        "job\t61\tAttempt three\tunknown\t-\t-\tcompleted\tcancelled\t1160\t1170\t1190\t0\n"
        "note\tAPI\tone omitted earlier attempt; data are incomplete\n";
    bool parsed = accounting_fixture(accounting, facts);
    accounting_expect(parsed && accounting_finish(accounting), "all outcomes and attempts parse", checks, &failures);
    if (parsed)
    {
        const AccountingJob* job = &accounting->jobs[0];
        accounting_expect(job->execution == 100 && job->exact, "execution is actual endpoint difference", checks, &failures);
        accounting_expect(job->phases[ACCOUNTING_SETUP] == 20, "PR setup without native tests", checks, &failures);
        accounting_expect(job->phases[ACCOUNTING_MEASUREMENT] == 35 && job->phases[ACCOUNTING_OVERLAP] == 5, "Compare/Time overlap charged once", checks, &failures);
        accounting_expect(job->phases[ACCOUNTING_CORRECTNESS] == 25 && job->phases[ACCOUNTING_UPLOAD] == 5 && job->phases[ACCOUNTING_OVERHEAD] == 5 && job->phases[ACCOUNTING_UNCLASSIFIED] == 5, "partition equals independent 100-second duration", checks, &failures);
        accounting_expect(job->missing_steps == 1 && job->feedback == 950, "missing measurements stay unknown, failed correctness counts", checks, &failures);
        accounting_expect(!accounting->jobs[1].allocated && accounting->jobs[1].execution == 0 && !accounting->jobs[1].observed, "skipped runner placeholders excluded", checks, &failures);
        const AccountingLogical* logical = &accounting->logical[0];
        accounting_expect(accounting->logical_count == 6 && accounting->run_count == 7 && logical->attempts == 2, "retry does not overweight run latency", checks, &failures);
        accounting_expect(logical->feedback == 50 && logical->completion == 250, "original run creation anchors feedback and completion", checks, &failures);
        accounting_expect(logical->resources[0] == 140 && logical->retry == 40 && logical->complete, "all attempt resources and retry category", checks, &failures);
        accounting_expect(logical->union_seconds == 140 && logical->completion - logical->union_seconds == 110, "wall occupation and retry gaps reconcile", checks, &failures);
        accounting_expect(logical->tail == 2 && logical->longest == 0, "tail and longest are different indicators", checks, &failures);
        accounting_expect(accounting->logical[1].completion == 120 && accounting->runs[2].fate == 2, "updated_at ignored and live candidate not invalidated", checks, &failures);
        const AccountingJob* lock = &accounting->jobs[4];
        accounting_expect(lock->execution == 100 && lock->phases[ACCOUNTING_LOCK_WAIT] == 10 && lock->phases[ACCOUNTING_OVERHEAD] == 10 && lock->phases[ACCOUNTING_SETUP] == 10 && lock->phases[ACCOUNTING_MEASUREMENT] == 70, "lock wait carved out from runner hook", checks, &failures);
        accounting_expect(lock->lock_hold == 90 && lock->lock_wait == 10, "lock hold is overlay", checks, &failures);
        const AccountingJob* partial = &accounting->jobs[5];
        accounting_expect(!partial->exact && partial->observed && partial->execution == 50 && partial->partial_steps == 1 && partial->phases[ACCOUNTING_MEASUREMENT] == 50, "running job available elapsed lower bound", checks, &failures);
        accounting_expect(accounting->logical[3].completion == BUSTER_ACCOUNTING_UNKNOWN && accounting->logical[3].feedback == BUSTER_ACCOUNTING_UNKNOWN && !accounting->logical[3].complete, "partial latency remains censored", checks, &failures);
        const AccountingJob* step_only = &accounting->jobs[6];
        accounting_expect(step_only->observed && !step_only->exact && step_only->execution == 60 && step_only->phases[ACCOUNTING_UNCLASSIFIED] == 0 && step_only->view_start == BUSTER_ACCOUNTING_UNKNOWN, "step-only lower bound excludes uncovered unknown gap", checks, &failures);
        accounting_expect(!accounting->logical[5].complete && accounting->logical[5].attempts == 1 && accounting->logical[5].retry == 20, "missing earlier attempts explicit", checks, &failures);
        accounting_expect(!accounting->logical[5].classes_known, "unknown runner class cannot support hosted capacity coefficient", checks, &failures);
        accounting_expect(accounting_peak(accounting) == 2, "parallel hosted intervals count once each", checks, &failures);
        FILE* rendered = tmpfile();
        accounting_expect(rendered != NULL, "report scratch stream", checks, &failures);
        if (rendered != NULL)
        {
            accounting_render(accounting, rendered);
            fflush(rendered);
            rewind(rendered);
            char report[32768];
            const size_t read = fread(report, 1, sizeof(report) - 1, rendered);
            report[read] = 0;
            accounting_expect(strstr(report, "Actual configured account hosted concurrency: **unknown**") != NULL, "unknown capacity never uses plan default", checks, &failures);
            accounting_expect(strstr(report, "| before |") != NULL && strstr(report, "| after |") != NULL && strstr(report, "| cancelled | 1 |") != NULL, "cohorts and non-success outcomes retained", checks, &failures);
            accounting_expect(strstr(report, "confirmed superseded waste is unknown") != NULL && strstr(report, "Failure-attributed rebuild work is also unknown") != NULL, "no invented causal waste", checks, &failures);
            fclose(rendered);
        }
    }
    accounting_destroy(accounting);

    static const char* const invalid[] = {
        "meta\t1\t-\t0\tadmin\n",
        "meta\t1\t-\t500\t-\n",
        "meta\t1\t-\t18446744073709551616\tadmin\n",
        "meta\t1\t-\t-\t-",
        "meta\t-1\t-\t-\t-\n",
        "meta\t1\t-\t-\t-\nmeta\t1\t-\t-\t-\n",
        "step\t1\tTest\tcompleted\tsuccess\t1\t2\n",
        "lock\t1\t2\t3\n",
        "meta\t1\t-\t-\t-\nunknown\t1\n",
        "meta\t1\t-\t-\t-\nnote\tAPI\tbad\rdata\n",
        "meta\t1\t-\t-\t-\nrun\t1\t0\tbuild.yml\tpr\tall\t-\tx\tcompleted\tsuccess\t1\t1\t1\tna\t1\n",
        "meta\t1\t-\t-\t-\nrun\t1\t1\tbuild.yml\tunknown\tall\t-\tx\tcompleted\tsuccess\t1\t1\t1\tna\t1\n",
        "meta\t1\t-\t-\t-\nrun\t1\t1\tbuild.yml\tpr\tall\twrong-sha\tx\tcompleted\tsuccess\t1\t1\t1\tna\t1\n",
        "meta\t200\t100\t-\t-\nrun\t1\t1\tbuild.yml\tpr\tbefore\t-\tx\tcompleted\tsuccess\t150\t150\t190\tna\t1\n",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        accounting = accounting_create();
        accounting_expect(!accounting_fixture(accounting, invalid[i]), "invalid facts rejected", checks, &failures);
        accounting_destroy(accounting);
    }
    const char* duplicate =
        "meta\t1200\t-\t500\tadmin-record\n"
        "run\t1\t1\tbuild.yml\tpr\tall\t-\tfeature\tcompleted\tsuccess\t1000\t1000\t1200\tna\t1\n"
        "job\t1\tfirst\thosted\tubuntu\t-\tcompleted\tsuccess\t1000\t1010\t1100\t1\n"
        "job\t1\trepeated page\thosted\tubuntu\t-\tcompleted\tsuccess\t1000\t1010\t1100\t1\n";
    accounting = accounting_create();
    accounting_expect(accounting_fixture(accounting, duplicate) && !accounting_finish(accounting), "duplicate job id rejected across pages/attempts", checks, &failures);
    accounting_destroy(accounting);
    const char* bad_step =
        "meta\t1200\t-\t-\t-\n"
        "run\t1\t1\tbuild.yml\tpr\tall\t-\tfeature\tcompleted\tsuccess\t1000\t1000\t1200\tna\t1\n"
        "job\t1\tfirst\thosted\tubuntu\t-\tcompleted\tsuccess\t1000\t1010\t1100\t1\n"
        "step\t1\tBuild compiler\tcompleted\tsuccess\t1010\t1020\n"
        "step\t1\tTest compiler\tcompleted\tsuccess\t1020\t1030\n";
    accounting = accounting_create();
    accounting_expect(!accounting_fixture(accounting, bad_step), "duplicate step number rejected", checks, &failures);
    accounting_destroy(accounting);
    const char* clipping =
        "meta\t1200\t-\t-\t-\n"
        "run\t1\t1\tbuild.yml\tpr\tall\t-\tfeature\tcompleted\tsuccess\t1000\t1000\t1200\tna\t0\n"
        "job\t1\tclipped\thosted\tubuntu\t-\tcompleted\tsuccess\t1000\t1010\t1100\t1\n"
        "step\t1\tBuild compiler\tcompleted\tsuccess\t900\t1020\n"
        "step\t2\tTest compiler\tcompleted\tsuccess\t1020\t1200\n"
        "job\t2\treversed\thosted\tubuntu\t-\tcompleted\ttimed_out\t1000\t1150\t1149\t0\n"
        "job\t3\tunallocated failure\tnone\tubuntu\t-\tcompleted\tfailure\t1000\t1000\t1000\t0\n";
    accounting = accounting_create();
    parsed = accounting_fixture(accounting, clipping) && accounting_finish(accounting);
    accounting_expect(parsed, "clipped and reversed timestamp fixture", checks, &failures);
    if (parsed)
    {
        accounting_expect(accounting->jobs[0].execution == 90 && accounting->jobs[0].phases[ACCOUNTING_SETUP] == 10 && accounting->jobs[0].phases[ACCOUNTING_CORRECTNESS] == 80, "step ranges clipped to independent job bounds", checks, &failures);
        accounting_expect(accounting->jobs[0].feedback == BUSTER_ACCOUNTING_UNKNOWN && accounting->jobs[1].feedback == BUSTER_ACCOUNTING_UNKNOWN, "out-of-bounds step and reversed failed job cannot manufacture feedback", checks, &failures);
        accounting_expect(!accounting->jobs[1].observed && accounting->jobs[1].execution == 0 && accounting->jobs[1].queue == 150, "reversed execution unknown, queue still available", checks, &failures);
        accounting_expect(accounting->jobs[2].feedback == BUSTER_ACCOUNTING_UNKNOWN && !accounting->logical[0].complete && accounting->logical[0].completion == BUSTER_ACCOUNTING_UNKNOWN, "unallocated failure and missing page cannot fake complete latency", checks, &failures);
    }
    accounting_destroy(accounting);
    accounting = accounting_create();
    accounting_expect(accounting_fixture(accounting, "meta\t1200\t-\t-\t-\n") && accounting_finish(accounting) && accounting->logical_count == 0, "empty sample valid and unknown", checks, &failures);
    accounting_destroy(accounting);
    accounting_expect(accounting_phase("Time native code") == ACCOUNTING_MEASUREMENT && accounting_phase("Compare candidates") == ACCOUNTING_MEASUREMENT && accounting_phase("Attribute overhead") == ACCOUNTING_MEASUREMENT, "all measurement families", checks, &failures);
    accounting_expect(accounting_phase("Gauge sqlite3.c's compile at one worker and at eight, by stage") == ACCOUNTING_MEASUREMENT && accounting_phase("Reproduce a sample of the Zen 5 model's rows") == ACCOUNTING_MEASUREMENT && accounting_phase("Reproduce the Zen 5 sample in two more processes") == ACCOUNTING_MEASUREMENT, "actual Gauge and Zen 5 reproduction measurements", checks, &failures);
    accounting_expect(accounting_phase("Early hosted feedback (not qualification)") == ACCOUNTING_CORRECTNESS && accounting_phase("Check that the early corpus detects a regression in each promised area") == ACCOUNTING_CORRECTNESS && accounting_phase("Preserve exact feedback records and reproductions") == ACCOUNTING_UPLOAD, "early feedback is first useful feedback, its records an upload", checks, &failures);
    accounting_expect(accounting_phase("Some future workflow step") == ACCOUNTING_UNCLASSIFIED, "new steps not silently categorized", checks, &failures);
    return failures;
}
