import os
import re
import signal
import subprocess
import sys
from pathlib import Path

binary, log_name, expected = sys.argv[1:]
env = dict(os.environ, BUSTER_TEST_JOBS='1')
process = subprocess.Popen([binary, 'test', '--verbose=1', '--ci=1'], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
def timeout_handler(signum, frame):
    raise TimeoutError('c_once_tests did not finish within the bounded slice')
signal.signal(signal.SIGALRM, timeout_handler)
signal.alarm(300)
found = False
try:
    with Path(log_name).open('w') as log:
        for line in process.stdout:
            log.write(line)
            if 'C_ONCE_PROBES_V1' in line or 'TEST_MODULE_TIMING' in line:
                print(line, end='', flush=True)
            if 'TEST_MODULE_TIMING' in line and 'module=c_once_tests ' in line:
                found = True
                if f'status={expected}' not in line:
                    raise RuntimeError(f'Unexpected owned module result: {line}')
                break
    if not found:
        raise RuntimeError('No c_once_tests result was emitted')
finally:
    signal.alarm(0)
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
print('Stopped after c_once_tests; later modules are unclaimed.', flush=True)
