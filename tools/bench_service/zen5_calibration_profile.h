/* zen5-calibration-v1 installed recipe profile, byte-identical to
 * profiles/zen5-calibration-v1.recipe (zen5_profile_test.py checks both and
 * every pinned tool digest). queue.c registers it; zen5_recipe.c reads its
 * budget, gap, workload and pins; zen5_stage.h fixes the workload and CPU
 * the broker's stage argv uses. queue.c admits and serves the recipe.
 */
#ifndef BUSTER_BENCH_SERVICE_ZEN5_CALIBRATION_PROFILE_H
#define BUSTER_BENCH_SERVICE_ZEN5_CALIBRATION_PROFILE_H
#define BQ_ZEN5_CALIBRATION_PROFILE \
    "schema=1\n" \
    "recipe=zen5-calibration-v1\n" \
    "repository=buster14a/buster\n" \
    "source-manifest=BQ-SOURCE-V1\n" \
    "status=served-by-broker-v2\n" \
    "source=base-equals-candidate\n" \
    "source-tree-file=.bq-source-tree\n" \
    "budget-seconds=2700\n" \
    "timing-reserve-seconds=600\n" \
    "trusted-builds=5\n" \
    "pairs=360\n" \
    "timed-children=720\n" \
    "interblock-gap-ns=2000000000\n" \
    "workload=tests/c_abi_cfuncs.c\n" \
    "cpu=2\n" \
    "pmu=tools/zen5_pmu_events_v1.json\n" \
    "pmu-sha256=066f01cbb33b2747b71f43b61a5aa8421becd4cc4a4a6b58082d304fc01950de\n" \
    "pmu-capture=tools/zen5_host_qualification.py\n" \
    "pmu-capture-sha256=7a6aa1f62109eae50fc1b1e19f9190b373e829195d5d7dba8677c4018b57ed86\n" \
    "pmu-common=tools/zen5_qualification_common.py\n" \
    "pmu-common-sha256=8fa8b0528eb93f7f7d3a94afbe021f4c0f22f4a4eab2b7f0d1d6b8483134a7b9\n" \
    "pmu-replay=tools/zen5_qualification_replay.py\n" \
    "pmu-replay-sha256=6b4ee48a99563aa8605b28b7351029a0b257042a2e8ab3ab434ac207ab9c5023\n" \
    "ab-authorized=false\n"
#endif
