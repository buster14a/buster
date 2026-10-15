#!/usr/bin/env python3
"""Durable native observations for one alternating pair of frozen binaries."""
import hashlib
import json
import os
from pathlib import Path
import signal
import sys
import time

root = Path(__file__).resolve().parent
pair = int(sys.argv[1])
assert 0 <= pair < 6
destination = root / 'ab-suite-durable'
destination.mkdir(exist_ok=True)
subjects = {
    'A': ('ide-baseline', '56221853cc5b11acb33e10ceca29484f7560bcf6',
          '3bbbad302908dba2fffe5c43967654f4df1f19d4b4aded7379d1a1cc5df62b60'),
    'B': ('ide-candidate-v2', 'bf29aed8e0b9369ee4e59b2e7315b842fae32945',
          'dfc06768803f10cc9e7f9775cd4f35aadf24b6fd1a92d83557eb39f9f9343df3'),
}
environment = dict(os.environ)
for name in ('BUSTER_TEST_JOBS', 'BUSTER_TEST_RETAIN_IMAGES'):
    environment.pop(name, None)
os.chdir(root / 'buster')
for arm in ('AB' if pair % 2 == 0 else 'BA'):
    filename, revision, expected_hash = subjects[arm]
    binary = root / 'frozen' / filename
    actual_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert actual_hash == expected_hash
    stem = destination / f'run-{arm}-{pair:02d}'
    receipt_path = stem.with_suffix('.json')
    assert not receipt_path.exists(), 'Never overwrite a measured run'
    argv = [str(binary), 'test', '--verbose=0']
    with stem.with_suffix('.stdout').open('xb') as stdout, stem.with_suffix('.stderr').open('xb') as stderr:
        started_utc = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
        start = time.monotonic()
        pid = os.fork()
        if pid == 0:
            os.setsid()
            os.dup2(stdout.fileno(), 1)
            os.dup2(stderr.fileno(), 2)
            os.execve(str(binary), argv, environment)
        print(f'START pair={pair} arm={arm} pid={pid}', flush=True)
        last_progress = start
        timed_out = False
        while True:
            child, status, usage = os.wait4(pid, os.WNOHANG)
            now = time.monotonic()
            if child == pid:
                break
            if now - start > 600:
                os.killpg(pid, signal.SIGKILL)
                child, status, usage = os.wait4(pid, 0)
                timed_out = True
                now = time.monotonic()
                break
            if now - last_progress >= 30:
                print(f'RUNNING pair={pair} arm={arm} elapsed={now-start:.1f}s', flush=True)
                last_progress = now
            time.sleep(0.05)
    result = {
        'schema': 'buster-native-pair-observation-v1', 'pair': pair, 'arm': arm,
        'started_utc': started_utc, 'argv': argv, 'revision': revision,
        'binary_sha256': actual_hash, 'exit_code': os.waitstatus_to_exitcode(status),
        'timed_out': timed_out, 'wall_seconds': now-start,
        'user_seconds': usage.ru_utime, 'system_seconds': usage.ru_stime,
        'max_rss_kib': usage.ru_maxrss, 'minor_faults': usage.ru_minflt,
        'major_faults': usage.ru_majflt, 'voluntary_switches': usage.ru_nvcsw,
        'involuntary_switches': usage.ru_nivcsw,
        'scope': 'wait4 for the exact child, including its reaped descendants',
    }
    receipt_path.write_text(json.dumps(result, indent=2) + '\n')
    print(f"DONE pair={pair} arm={arm} wall={now-start:.3f}s exit={result['exit_code']}", flush=True)
    assert result['exit_code'] == 0 and not timed_out
