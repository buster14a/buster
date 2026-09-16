from pathlib import Path
p = Path('android/run_tests.sh')
s = p.read_text()
old = 'timeout_seconds=${BUSTER_ANDROID_TEST_TIMEOUT_SECONDS:-60}'
new = '''# This watchdog bounds the complete suite, not each adb operation. Debug
# compiler-driver fixtures can exceed one minute while still making progress.
# A terminal result still ends monitoring immediately; missing results fail.
timeout_seconds=${BUSTER_ANDROID_TEST_TIMEOUT_SECONDS:-180}'''
assert s.count(old) == 1
p.write_text(s.replace(old,new))
p = Path('android/run_tests_test.sh')
s = p.read_text()
s = s.replace('    local state=$test_root/$scenario\n    local start_ns', '    local selected_timeout=${4-3}\n    local case_name=${5:-$scenario}\n    local state=$test_root/$case_name\n    local start_ns', 1)
old = '        BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=3 \\\n'
assert s.count(old) == 1
s = s.replace(old, '        BUSTER_ANDROID_TEST_TIMEOUT_SECONDS="$selected_timeout" \\\n')
s = s.replace('"$scenario" "$status" "$elapsed_ms"', '"$case_name" "$status" "$elapsed_ms"')
old = '    assert_no_owned_producer "$state"\n\n    case "$scenario" in'
new = '''    assert_no_owned_producer "$state"
    if [[ $scenario != install_failure && $scenario != launch_failure ]]; then
        assert_log_contains "ANDROID_MONITOR_START config=standalone timeout_seconds=${selected_timeout:-180}" "$state"
    fi

    case "$scenario" in'''
assert s.count(old) == 1
s = s.replace(old,new)
s = s.replace('"$scenario" "$marker_tail_ms"', '"$case_name" "$marker_tail_ms"')
old = 'run_case install_failure 24 2000\n'
new = '''run_case install_failure 24 2000
# The normal whole-suite budget must not delay either terminal result. Keep
# the short explicit timeout/malformed-marker controls above as real failures.
run_case success 0 2000 '' default_deadline_success
run_case failure 1 2000 '' default_deadline_failure
run_case success 0 2000 5 override_deadline_success
'''
assert s.count(old) == 1
s = s.replace(old,new)
p.write_text(s)
p=Path('docs/agents/testing.md')
s=p.read_text()
old='  `status=not-run` is itself evidence of an incomplete run.\n'
new=old+'''  `android/run_tests.sh` bounds each complete suite with a 180-second monitor
  watchdog, independently of adb command/install/boot deadlines. Debug compiler
  fixtures exceeded the former 60-second budget while still reporting progress
  in PR #687's run `35157195321`; this is a correctness-suite execution budget,
  not a throughput threshold. `BUSTER_ANDROID_TEST_TIMEOUT_SECONDS` explicitly
  overrides it. Terminal markers end monitoring immediately, and timeout,
  malformed or missing markers still fail; the fake monitor suite checks the
  default and override without lengthening its short timeout failure controls.
'''
assert s.count(old) == 1
p.write_text(s.replace(old,new))
