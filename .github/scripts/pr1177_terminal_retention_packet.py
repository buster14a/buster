#!/usr/bin/env python3
"""Disposable PR #1177 implementation/validation packet; never merged as tooling."""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import time

ROOT = Path.cwd()
BROKER = 'tools/bench_service/systemd_broker.c'
WORKER = 'tools/bench_service/worker_linux.c'
TESTS = 'tools/bench_service/tests.c'
DOC = 'tools/bench_service/deploy/README.md'


def replace(path, before, after):
    p = ROOT / path
    text = p.read_text()
    if text.count(before) != 1:
        raise RuntimeError(f'{path}: expected one exact replacement, got {text.count(before)}')
    p.write_text(text.replace(before, after, 1))


def regressions():
    for path, expected in {
        BROKER: 'ea8c3b16138619f0e3eee9204a71c6989a31ccb9',
        WORKER: 'c78e7dcdd5809572fc4a4df0d278fb5a279aabf0',
        TESTS: '33e2a4b9700347dbe8ff6662e0d3103cfe20a8cc',
    }.items():
        data = (ROOT / path).read_bytes()
        actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        if actual != expected:
            raise RuntimeError(f'{path}: source changed: {actual}')
    replace(BROKER,
        '        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--service-output") ==\n'
        '                        (stage == BQ_BROKER_THROUGHPUT_STAGE));',
        '        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--service-output") ==\n'
        '                        (stage == BQ_BROKER_THROUGHPUT_STAGE));\n'
        '        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--property=CollectMode=inactive") ==\n'
        '                        (stage == BQ_BROKER_OUTER));\n'
        '        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--collect") ==\n'
        '                        (stage != BQ_BROKER_OUTER));')
    replace(TESTS,
        '    snprintf(fixture->fake.observed.kill_mode, sizeof(fixture->fake.observed.kill_mode), "%s", "control-group");',
        '    snprintf(fixture->fake.observed.kill_mode, sizeof(fixture->fake.observed.kill_mode), "%s", "control-group");\n'
        '    snprintf(fixture->fake.observed.collect_mode, sizeof(fixture->fake.observed.collect_mode), "%s", "inactive");')
    replace(TESTS,
        '    *status = fake->collect_on_join && fake->completion != BQ_WORKER_SUCCEEDED ? 1 << 8 : 0;',
        '    *status = fake->completion == BQ_WORKER_SUCCEEDED ? 0 :\n'
        '              fake->completion == BQ_WORKER_OOM ? SIGKILL : 1 << 8;')
    text = (ROOT / TESTS).read_text()
    begin = text.index('    for (u32 collected = 0; collected < 3; collected += 1)',
                       text.index('BUSTER_GLOBAL_LOCAL void bq_test_worker_drained_unit(void)'))
    end = text.index('\n    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))', begin)
    replace(TESTS, text[begin:end], '''    struct { BqWorkerResult result; BqError reason; } cases[] = {
        {BQ_WORKER_SUCCEEDED, BQ_NOT_FOUND},
        {BQ_WORKER_EXECUTION_FAILED, BQ_WORKER_FAILED},
        {BQ_WORKER_OOM, BQ_WORKER_OOM_FAILURE},
        {BQ_WORKER_TIMED_OUT, BQ_WORKER_TIMEOUT}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        for (u32 collected = 0; collected < 2; collected += 1)
        {
            if (bq_test_worker_begin(&fixture, cases[index].result, false))
            {
                BqRequest request = bq_test_real_request(146 + index * 2 + collected);
                u64 id = 0;
                fixture.fake.drain_on_join = true;
                fixture.fake.collect_on_join = collected != 0;
                /* A failed outer unit must be retained. Losing its manager
                 * result is an identity/evidence failure, not an exit-code verdict. */
                bool lost_result = collected && cases[index].result != BQ_WORKER_SUCCEEDED;
                BqError expected_error = lost_result ? BQ_WORKER_MISMATCH : BQ_OK;
                BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                         bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == expected_error);
                BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
                BqError expected_reason = lost_result ? BQ_WORKER_MISMATCH : cases[index].reason;
                BQ_CHECK(job && bq_failure_evidence(&fixture.material.queue.queue, job) == expected_reason);
                if (lost_result)
                {
                    BQ_CHECK(job && job->phase != BQ_FINISHED &&
                             fixture.material.queue.queue.needs_reconciliation &&
                             fixture.material.queue.queue.state.active_id == id);
                }
                else
                {
                    BqOutcome expected = cases[index].result == BQ_WORKER_SUCCEEDED ? BQ_SUCCEEDED : BQ_FAILED;
                    BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == expected &&
                             fixture.fake.joins >= 2 && !fixture.fake.observed.cgroup[0] &&
                             fixture.fake.observed.unit_found == (collected == 0));
                }
                /* Check the durable reason after replay, not just the fake's result. */
                bq_close(&fixture.material.queue.queue);
                BQ_CHECK(bq_open(&fixture.material.queue.queue, fixture.material.queue.path) == BQ_OK);
                job = bq_job(&fixture.material.queue.queue.state, id);
                BQ_CHECK(job && bq_failure_evidence(&fixture.material.queue.queue, job) == expected_reason);
                bq_test_worker_end(&fixture);
            }
        }
    }''')
    # Keep the existing invocation/cgroup replacement controls and add policy controls.
    text = (ROOT / TESTS).read_text()
    begin = text.index('BUSTER_GLOBAL_LOCAL void bq_test_worker_drained_unit(void)')
    end = text.index('BUSTER_GLOBAL_LOCAL void bq_test_worker_term_grace(void)', begin)
    section = text[begin:end]
    section = section.replace(
        '        BQ_CHECK(bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &identity, false));',
        '        BQ_CHECK(bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &identity, false));\n'
        '        BqWorkerObserved wrong_policy = identity;\n'
        '        snprintf(wrong_policy.collect_mode, sizeof(wrong_policy.collect_mode), "%s", "inactive-or-failed");\n'
        '        BQ_CHECK(!bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &wrong_policy, false));\n'
        '        wrong_policy.collect_mode[0] = 0;\n'
        '        BQ_CHECK(!bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &wrong_policy, false));', 1)
    section = section.replace(
        '        snprintf(drained.invocation_id, sizeof(drained.invocation_id), "%s",\n',
        '        wrong_policy = drained;\n'
        '        snprintf(wrong_policy.collect_mode, sizeof(wrong_policy.collect_mode), "%s", "inactive-or-failed");\n'
        '        BQ_CHECK(!bq_worker_instance_matches(&fixture.config, &identity, &wrong_policy));\n'
        '        snprintf(drained.invocation_id, sizeof(drained.invocation_id), "%s",\n', 1)
    replace(TESTS, text[begin:end], section)


def implementation():
    replace(BROKER,
        '        bq_broker_common_sandbox(command);\n',
        '        /* Retain failed outer units until their exact manager Result is read.\n'
        '         * Only stage units use aggressive collection. */\n'
        '        if (request->stage == BQ_BROKER_OUTER)\n'
        '            bq_broker_add(command, "--property=CollectMode=inactive");\n'
        '        bq_broker_common_sandbox(command);\n')
    replace(BROKER, '        "PrivateNetwork"};', '        "PrivateNetwork", "CollectMode"};')
    replace(BROKER,
        '              bq_broker_field_equals(output, "PrivateNetwork", "yes") &&',
        '              bq_broker_field_equals(output, "PrivateNetwork", "yes") &&\n'
        '              bq_broker_field_equals(output, "CollectMode", request->stage == BQ_BROKER_OUTER ?\n'
        '                                      "inactive" : "inactive-or-failed") &&')
    replace(WORKER,
        '              !strcmp(observed->user, "buster-bench") && !strcmp(observed->group, "buster-bench") &&',
        '              !strcmp(observed->user, "buster-bench") && !strcmp(observed->group, "buster-bench") &&\n'
        '              !strcmp(observed->collect_mode, "inactive") &&')
    replace(WORKER,
        '                   observed->result != BQ_WORKER_RUNNING &&',
        '                   observed->result != BQ_WORKER_RUNNING &&\n'
        '                   !strcmp(observed->collect_mode, "inactive") &&')
    replace(WORKER,
        '            /* --wait reports the service exit status, while --collect may\n'
        '             * remove its manager record before the post-join observation. */\n'
        '            if (error == BQ_OK)\n'
        '                observed.result = WIFEXITED(status) && WEXITSTATUS(status) == 0 ?\n'
        '                                  BQ_WORKER_SUCCEEDED : BQ_WORKER_EXECUTION_FAILED;',
        '            /* CollectMode=inactive permits successful outer units to disappear,\n'
        '             * but retains failures and their exact Result. A missing failed\n'
        '             * invocation cannot be classified from the launcher status. */\n'
        '            if (error == BQ_OK && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))\n'
        '                error = BQ_WORKER_MISMATCH;\n'
        '            if (error == BQ_OK) observed.result = BQ_WORKER_SUCCEEDED;')
    replace(DOC,
        'that the original cgroup leaf is absent. For a collected unit, it uses the\n'
        'bounded `systemd-run --wait` process status for the outcome before finalizing;\n'
        '`LoadState=not-found` does not carry the full set of active-unit properties.',
        'that the original cgroup leaf is absent. Outer units explicitly use\n'
        '`CollectMode=inactive`, verified by the worker and signal broker, so failed\n'
        'units retain their exact manager `Result` for OOM/timeout attribution.\n'
        'Only a collected successful outer unit may use the bounded\n'
        '`systemd-run --wait` zero exit status before finalizing. A missing unit\n'
        'with a nonzero or signalled launcher status is an evidence mismatch and\n'
        'remains quarantined; it is never classified as a generic execution failure.\n'
        '`LoadState=not-found` does not carry the full set of active-unit properties.\n'
        'Stage units retain `CollectMode=inactive-or-failed`; this change adds no\n'
        'manager reset operation or automatic removal of retained failed outer units.')


def live():
    if os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted':
        raise RuntimeError('This disposable probe requires a GitHub-hosted runner')
    directory = Path(os.environ['RUNNER_TEMP']) / 'pr1177-retention'
    directory.mkdir(exist_ok=True)
    source = directory / 'payload.c'
    binary = directory / 'payload'
    source.write_text('''#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    int result = 42;
    if (argc == 2 && !strcmp(argv[1], "success")) result = 0;
    else if (argc == 2 && !strcmp(argv[1], "timeout"))
        for (;;) pause();
    else if (argc == 2 && !strcmp(argv[1], "oom-kill"))
    {
        for (;;)
        {
            volatile unsigned char *p = malloc(1024 * 1024);
            if (!p) break;
            for (unsigned i = 0; i < 1024 * 1024; i += 4096) p[i] = 1;
        }
    }
    return result;
}
''')
    subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run(['systemctl', '--version'], check=True)
    units = []
    checks = 0
    def show(unit):
        p = subprocess.run(['sudo', 'systemctl', 'show', '--no-pager', '--property=LoadState',
                            '--property=ActiveState', '--property=Result', '--property=CollectMode',
                            '--property=InvocationID', unit], text=True, capture_output=True, timeout=10)
        return dict(line.split('=', 1) for line in p.stdout.splitlines() if '=' in line)
    try:
        for mode in ('inactive', 'inactive-or-failed'):
            for outcome in ('success', 'exit-code', 'oom-kill', 'timeout'):
                unit = f'pr1177-{os.environ["GITHUB_RUN_ID"]}-{checks}.service'
                units.append(unit)
                command = ['sudo', 'systemd-run', '--quiet', '--wait', '--service-type=exec',
                           '--unit=' + unit, '--uid=' + str(os.getuid()),
                           '--property=CollectMode=' + mode, '--property=MemoryMax=33554432',
                           '--property=MemorySwapMax=0', '--property=TasksMax=16',
                           '--property=RuntimeMaxSec=2s', '--property=TimeoutStopSec=1s',
                           '--property=NoNewPrivileges=yes', '--property=KillMode=control-group',
                           str(binary), outcome]
                p = subprocess.run(command, text=True, capture_output=True, timeout=20)
                if (p.returncode == 0) != (outcome == 'success'):
                    raise RuntimeError(f'{unit}: unexpected launcher status {p.returncode}: {p.stderr}')
                retained = mode == 'inactive' and outcome != 'success'
                time.sleep(0.3)
                observed = show(unit)
                if retained:
                    if observed.get('LoadState') != 'loaded' or observed.get('ActiveState') != 'failed' or observed.get('Result') != outcome or observed.get('CollectMode') != mode or len(observed.get('InvocationID', '')) != 32:
                        raise RuntimeError(f'{unit}: did not preserve {outcome}: {observed}')
                    first = observed['InvocationID']
                    time.sleep(0.3)
                    observed = show(unit)
                    if observed.get('Result') != outcome or observed.get('InvocationID') != first:
                        raise RuntimeError(f'{unit}: result/invocation changed: {observed}')
                else:
                    for _ in range(50):
                        if observed.get('LoadState') == 'not-found': break
                        time.sleep(0.1)
                        observed = show(unit)
                    if observed.get('LoadState') != 'not-found':
                        raise RuntimeError(f'{unit}: did not collect: {observed}')
                checks += 1
                print(f'RETENTION_LIVE mode={mode} outcome={outcome} retained={retained} status={p.returncode} pass', flush=True)
    finally:
        for unit in units:
            subprocess.run(['sudo', 'systemctl', 'stop', unit], capture_output=True, timeout=10)
            subprocess.run(['sudo', 'systemctl', 'reset-failed', unit], capture_output=True, timeout=10)
    if checks != 8: raise RuntimeError(f'incomplete live probe: {checks}')
    print('RETENTION_LIVE checks=8 result=pass', flush=True)


if __name__ == '__main__':
    actions = {'regressions': regressions, 'implementation': implementation, 'live': live}
    if len(sys.argv) != 2 or sys.argv[1] not in actions:
        raise SystemExit('expected regressions, implementation, or live')
    actions[sys.argv[1]]()
