from pathlib import Path
import subprocess
import sys

r = Path(sys.argv[1])
conflicts = subprocess.check_output(['git', '-C', str(r), 'diff', '--name-only', '--diff-filter=U'], text=True).splitlines()
assert set(conflicts) == {'.github/workflows/ios-monitor-tests.yml', 'android/run_tests.sh', 'android/run_tests_test.sh'}, conflicts

p = r / 'android/run_tests.sh'
s = p.read_text()
a = s.index('<<<<<<< HEAD\n')
b = s.index('>>>>>>> refs/rewritten/onto\n', a) + len('>>>>>>> refs/rewritten/onto\n')
s = s[:a] + '''# This watchdog bounds the complete suite, not each adb operation. Debug
# compiler-driver fixtures can exceed one minute while still making progress.
# A terminal result still ends monitoring immediately; missing results fail.
timeout_seconds=${BUSTER_ANDROID_TEST_TIMEOUT_SECONDS:-180}
# A payload that merely fits its deadline today crosses it on a slower runner or
# after new tests are added. Report the remaining margin as a percentage of the
# deadline so thinning headroom is visible before it becomes a red lane.
headroom_warning_percent=${BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT:-25}
''' + s[b:]
p.write_text(s)

p = r / '.github/workflows/ios-monitor-tests.yml'
s = p.read_text()
a = s.index('<<<<<<< HEAD\n')
b = s.index('>>>>>>> refs/rewritten/onto\n', a) + len('>>>>>>> refs/rewritten/onto\n')
ours, theirs = s[a + len('<<<<<<< HEAD\n'):b - len('>>>>>>> refs/rewritten/onto\n')].split('=======\n')
theirs = theirs.replace('Reproduce: `bash tests/mobile_ci_scripts_test.sh`', 'Reproduce: `python3 tests/ios_hosted_signing_budget_test.py`, `bash tests/mobile_ci_scripts_test.sh`')
s = s[:a] + theirs + s[b:]
p.write_text(s)

p = r / 'android/run_tests_test.sh'
s = p.read_text()
s = s.replace('    local selected_timeout=${4-3}\n', '')
a = s.index('<<<<<<< HEAD\n')
b = s.index('>>>>>>> refs/rewritten/onto\n', a) + len('>>>>>>> refs/rewritten/onto\n')
ours, theirs = s[a + len('<<<<<<< HEAD\n'):b - len('>>>>>>> refs/rewritten/onto\n')].split('=======\n')
s = s[:a] + theirs + s[b:]
needle = '    esac\n\n    start_ns=$(date +%s%N)'
assert s.count(needle) == 1
s = s.replace(needle, '''    esac
    # Preserve per-scenario defaults, explicit overrides, and an empty value
    # that exercises the production wrapper's normal whole-suite deadline.
    timeout_seconds=${4-$timeout_seconds}
    local monitor_timeout_seconds=${timeout_seconds:-180}

    start_ns=$(date +%s%N)''')
s = s.replace('${selected_timeout:-180}', '$monitor_timeout_seconds')
s = s.replace("assert_log_matches 'ANDROID_MONITOR_RESULT config=standalone reader_status=10 producer_status=[0-9]+ timeout_seconds=3 elapsed_seconds=[0-9]+ headroom_seconds=[0-9]+ headroom_warning=no$'", 'assert_log_matches "ANDROID_MONITOR_RESULT config=standalone reader_status=10 producer_status=[0-9]+ timeout_seconds=$monitor_timeout_seconds elapsed_seconds=[0-9]+ headroom_seconds=[0-9]+ headroom_warning=no$"')
s = s.replace("grep -qF 'of its 3s deadline'", 'grep -qF "of its ${monitor_timeout_seconds}s deadline"')
s = s.replace("assert_log_matches 'ANDROID_MONITOR_RESULT config=standalone reader_status=10 producer_status=[0-9]+ timeout_seconds=6 elapsed_seconds=[0-9]+ headroom_seconds=[0-9]+ headroom_warning=yes$'", 'assert_log_matches "ANDROID_MONITOR_RESULT config=standalone reader_status=10 producer_status=[0-9]+ timeout_seconds=$monitor_timeout_seconds elapsed_seconds=[0-9]+ headroom_seconds=[0-9]+ headroom_warning=yes$"')
s = s.replace(r"assert_log_matches 'warning: Android standalone payload used [0-9]+s of its 6s deadline; [0-9]+s of headroom remain \(warning margin 4s at 75%\)'", r'assert_log_matches "warning: Android standalone payload used [0-9]+s of its ${monitor_timeout_seconds}s deadline; [0-9]+s of headroom remain \(warning margin $((monitor_timeout_seconds * headroom_percent / 100))s at ${headroom_percent}%\)"')
s = s.replace('run_case success 0 2000 5 override_deadline_success\n', 'run_case success 0 2000 5 override_deadline_success\nrun_case slow_success 0 6000 8 override_deadline_slow_success\n')
assert '<<<<<<<' not in s
assert 'selected_timeout' not in s
p.write_text(s)
