#!/usr/bin/env python3
"""One hosted, matched comparison/candidate-only pair for issue #2032.

Diagnostic branch only. Executes the actual production workflow bodies in one
checkout, with one Clang installation and sequential same-path builds. No timing
threshold or end-to-end speedup acceptance is implied by this single pair.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import textwrap
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def step(name):
    text = Path('.github/workflows/ci.yml').read_text()
    block = text.split('      - name: ' + name + '\n', 1)[1].split('      - name:', 1)[0]
    return textwrap.dedent(block.split('        run: |\n', 1)[1])


def run(command, env, log):
    with log.open('w') as output:
        subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT, check=True)
    print(log.read_text(), flush=True)


def main():
    checkout = Path.cwd()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode().strip()
    destination = Path(os.environ['RUNNER_TEMP']) / 'issue-2032-matched'
    destination.mkdir()
    rows = []
    for mode in ('comparison', 'candidate-only'):
        started = time.monotonic()
        temporary = destination / mode
        temporary.mkdir()
        env = dict(os.environ, RUNNER_TEMP=str(temporary), GITHUB_WORKSPACE=str(checkout),
                   GITHUB_ENV=str(temporary / 'exports'), GITHUB_STEP_SUMMARY=str(temporary / 'summary'),
                   BASELINE_REVISION=revision, EVENT_NAME='workflow_dispatch' if mode == 'comparison' else 'merge_group',
                   COMPARISON_REQUESTED='true' if mode == 'comparison' else 'false')
        print('PROBE_START', mode, revision, flush=True)
        run(['bash', '--noprofile', '--norc', '-c', step('Bootstrap candidate and select reference build driver')],
            env, temporary / 'bootstrap.log')
        env.update(dict(line.split('=', 1) for line in (temporary / 'exports').read_text().splitlines()))
        driver_sha = sha(checkout / 'build/analyzer-driver')
        run(['bash', '--noprofile', '--norc', '-c', step('Exercise analyzer failure and coverage controls')],
            env, temporary / 'controls.log')
        run(['bash', '--noprofile', '--norc', '-c', step('Configure the authoritative split-source database')],
            env, temporary / 'configure.log')
        database_sha = sha(checkout / 'build/analyzer-tree/compile_commands.json')
        campaign_started = time.monotonic()
        run(['bash', '--noprofile', '--norc', '-c', step('Compare reference analysis and aggregate all module shards')],
            env, temporary / 'campaign.log')
        campaign_seconds = time.monotonic() - campaign_started
        full = temporary / 'buster-analyzer/full'
        diagnostics = {str(p.relative_to(full)): sha(p) for p in sorted(full.rglob('unit-*.log'))}
        log = (temporary / 'campaign.log').read_text()
        metrics = [line for line in log.splitlines() if line.startswith(('ANALYZE_RUN ', 'ANALYZE_BASELINE ', 'ANALYZE_AGGREGATE '))]
        assert diagnostics and any(line.startswith('ANALYZE_RUN ') and 'status=pass' in line for line in metrics)
        assert any(line.startswith('ANALYZE_BASELINE ') for line in metrics) == (mode == 'comparison')
        assert (full / 'baseline.log').exists() == (mode == 'comparison')
        selection = (temporary / 'buster-analyzer/comparison-selection.txt').read_text()
        assert ('reason=requested' if mode == 'comparison' else 'reason=same-driver-merge-group') in selection
        row = dict(mode=mode, source_revision=revision, driver_sha256=driver_sha,
                   database_sha256=database_sha, diagnostics=diagnostics, eligible_logs=len(diagnostics),
                   metrics=metrics, campaign_seconds=campaign_seconds,
                   analyzer_scenario_seconds=time.monotonic() - started, selection=selection)
        rows.append(row)
        (destination / 'results.json').write_text(json.dumps(rows, sort_keys=True, indent=2) + '\n')
        print('PROBE_RESULT', json.dumps({k: v for k, v in row.items() if k != 'diagnostics'}), flush=True)
    assert rows[0]['database_sha256'] == rows[1]['database_sha256'], 'database commands changed'
    assert rows[0]['diagnostics'] == rows[1]['diagnostics'], 'candidate diagnostics or inventory changed'
    assert rows[0]['driver_sha256'] == rows[1]['driver_sha256'], 'same-root rebuilt drivers differ'
    print('PROBE_MATCHED_SUCCESS identical_driver=true identical_database=true identical_inventory_and_diagnostics=true', flush=True)


if __name__ == '__main__':
    main()
