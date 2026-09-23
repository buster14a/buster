#!/usr/bin/env python3
"""One finite, temporary hosted investigation; no expected value comes from Buster."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

# expected[0], expected[1] are independently authored unsigned-long-long observations.
CASES = [
    ("+((c) ? (signed char)(x) : (unsigned char)(z))", "int", -1, 0, 255,
     ["00000000000000ff", "ffffffffffffffff"]),
    ("-((c) ? (signed char)(x) : (unsigned char)(z))", "int", -128, 0, 255,
     ["ffffffffffffff01", "0000000000000080"]),
    ("-((c) ? (unsigned short)(z) : (short)(y))", "int", 0, -7, 65535,
     ["0000000000000007", "ffffffffffff0001"]),
    ("-((c) ? (signed char)(x) : (unsigned int)(z))", "unsigned int", -1, 0, 2,
     ["00000000fffffffe", "0000000000000001"]),
    ("(unsigned short)-((c) ? (signed char)(x) : (unsigned int)(z))", "unsigned short", -1, 0, 2,
     ["000000000000fffe", "0000000000000001"]),
    ("-((c) ? (unsigned int)(z) : (unsigned long long)(y))", "unsigned long long", 0, 3, 2147483648,
     ["fffffffffffffffd", "ffffffff80000000"]),
    ("(c) ? -(signed char)(x) : (unsigned char)(z)", "int", -128, 0, 255,
     ["00000000000000ff", "0000000000000080"]),
    ("~((c) ? (signed char)(x) : (unsigned int)(z))", "unsigned int", -1, 0, 2147483648,
     ["000000007fffffff", "0000000000000000"]),
]
COMMON = ["-std=c11", "-O0", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-fno-pic"]


def generate(root):
    src = root / "sources"
    src.mkdir(parents=True, exist_ok=True)
    header = ['#ifndef M8_CASES_H', '#define M8_CASES_H',
              '_Static_assert(__CHAR_BIT__ == 8, "M8 byte model");',
              '_Static_assert(sizeof(short) == 2 && sizeof(int) == 4, "M8 integer model");',
              '_Static_assert(sizeof(long) == 8 && sizeof(void *) == 8, "M8 LP64 model");',
              '_Static_assert(sizeof(unsigned long long) == 8, "M8 observer width");',
              'void m8_record(int route, int row, int choice, unsigned long long value);',
              'void m8_emit_static(void);', 'void m8_emit_runtime(void);']
    ice = ['#include "cases.h"']
    stat = ['#include "cases.h"']
    runtime = ['#include "cases.h"']
    caller = ['#include "cases.h"', 'void m8_emit_runtime(void)', '{']
    static_body = ['void m8_emit_static(void)', '{']
    manifest = []
    for row, (expr, typ, x, y, z, expected) in enumerate(CASES):
        signature = f'unsigned long long m8_eval_{row}(int c, int x, int y, unsigned long long z)'
        header.append(signature + ';')
        runtime += [signature, '{', '    (void)c;', '    (void)x;', '    (void)y;', '    (void)z;',
                    f'    return (unsigned long long)({expr});', '}']
        for choice in range(2):
            bindings = {'c': str(choice), 'x': str(x), 'y': str(y), 'z': str(z) + 'ULL'}
            literal = re.sub(r'\b[cxyz]\b', lambda m: bindings[m.group()], expr)
            ice += [f'_Static_assert((unsigned long long)({literal}) == 0x{expected[choice]}ULL, "M8 value {row}/{choice}");',
                    f'_Static_assert(_Generic(({literal}), {typ}: 1, default: 0), "M8 type {row}/{choice}");']
            stat.append(f'static unsigned long long m8_s_{row}_{choice} = (unsigned long long)({literal});')
            static_body.append(f'    m8_record(0, {row}, {choice}, m8_s_{row}_{choice});')
            caller += ['    {', f'        volatile int c = {choice};', f'        volatile int x = {x};',
                       f'        volatile int y = {y};', f'        volatile unsigned long long z = {z}ULL;',
                       f'        m8_record(1, {row}, {choice}, m8_eval_{row}(c, x, y, z));', '    }']
            manifest.append({'row': row, 'choice': choice, 'expression': literal, 'type': typ, 'expected_hex': expected[choice]})
    header.append('#endif')
    static_body += ['    return;', '}']
    caller += ['    return;', '}']
    files = {'cases.h': header, 'ice.c': ice, 'static.c': stat + static_body,
             'runtime.c': runtime, 'caller.c': caller,
             'same.c': ['#include "runtime.c"', '#include "caller.c"'],
             'wrong.c': ['#include "cases.h"', '_Static_assert((1 ? -1 : 0U) == 0, "M8_wrong_expected_value");'],
             'nonice.c': ['#include "cases.h"', 'int m8_nonice(int x)', '{',
                          '    _Static_assert(x ? 1 : 0, "M8_not_an_ICE");', '    return 0;', '}'],
             'observer.c': ['#include <stdio.h>', '#include "cases.h"',
                            'void m8_record(int route, int row, int choice, unsigned long long value)', '{',
                            '    printf("%d %d %d %016llx\\n", route, row, choice, value);', '    return;', '}',
                            'int main(void)', '{', '#ifndef M8_NO_STATIC', '    m8_emit_static();', '#endif',
                            '    m8_emit_runtime();', '    return 0;', '}']}
    for name, lines in files.items():
        (src / name).write_text('\n'.join(lines) + '\n')
    (root / 'cases.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (root / 'source-sha256.json').write_text(json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(src.iterdir())}, indent=2) + '\n')
    return src


class Runner:
    def __init__(self, root, phase):
        self.root = root
        self.phase = phase
        self.records = []
        self.results = []

    def run(self, name, argv, timeout=60):
        log = self.root / self.phase / name
        log.mkdir(parents=True, exist_ok=False)
        (log / 'argv.json').write_text(json.dumps([str(x) for x in argv], indent=2) + '\n')
        timed_out = False
        with (log / 'stdout').open('wb') as out, (log / 'stderr').open('wb') as err:
            try:
                child = subprocess.Popen([str(x) for x in argv], stdout=out, stderr=err, start_new_session=True)
                try:
                    status = child.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    os.killpg(child.pid, signal.SIGKILL)
                    status = child.wait()
            except OSError as exc:
                err.write(str(exc).encode())
                status = 127
        record = {'name': name, 'argv': [str(x) for x in argv], 'status': status, 'timed_out': timed_out}
        self.records.append(record)
        (log / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
        return record, (log / 'stdout').read_text(errors='replace'), (log / 'stderr').read_text(errors='replace')

    def check(self, name, passed, detail=''):
        self.results.append({'name': name, 'pass': bool(passed), 'detail': detail})
        print(('PASS ' if passed else 'FAIL ') + name + (' ' + detail if detail else ''), flush=True)
        return bool(passed)

    def command(self, name, argv, timeout=60):
        rec, out, err = self.run(name, argv, timeout)
        return self.check(name, rec['status'] == 0 and not rec['timed_out'], '' if rec['status'] == 0 else err[-1800:])

    def reject(self, name, argv, marker):
        rec, out, err = self.run(name, argv)
        return self.check(name, 0 < rec['status'] < 126 and not rec['timed_out'] and marker in err, err[-800:])

    def execute(self, name, exe, routes):
        rec, out, err = self.run(name, [exe], 10)
        expected = ''.join(f'{route} {row} {choice} {data[5][choice]}\n'
                           for route in routes for row, data in enumerate(CASES) for choice in range(2))
        (self.root / self.phase / name / 'expected.stdout').write_text(expected)
        return self.check(name, rec['status'] == 0 and not rec['timed_out'] and out == expected,
                          '' if out == expected else 'output differs from fixed independent table')

    def finish(self):
        summary = {'phase': self.phase, 'checks': self.results, 'commands': self.records,
                   'passed': sum(x['pass'] for x in self.results), 'failed': sum(not x['pass'] for x in self.results)}
        (self.root / (self.phase + '.json')).write_text(json.dumps(summary, indent=2) + '\n')
        print(json.dumps({k: summary[k] for k in ('phase', 'passed', 'failed')}), flush=True)
        return 1 if summary['failed'] else 0


def external(root, src, runner):
    for tool in ['clang', 'gcc', 'ld', 'objdump', 'uname']:
        runner.command('identity-' + tool, [tool, '-a'] if tool == 'uname' else [tool, '--version'])
    runner.command('clang-target', ['clang', '-print-target-triple'])
    runner.command('gcc-target', ['gcc', '-dumpmachine'])
    objects = root / 'objects'
    objects.mkdir(exist_ok=True)
    runner.command('observer', ['clang', *COMMON, '-c', src / 'observer.c', '-o', objects / 'observer.o'])
    runner.command('observer-runtime', ['clang', *COMMON, '-DM8_NO_STATIC', '-c', src / 'observer.c', '-o', objects / 'observer-runtime.o'])
    for cc in ['clang', 'gcc']:
        for opt in ['O0', 'O2']:
            label = cc + '-' + opt
            flags = [cc, *COMMON, '-' + opt, '-nostdinc', '-Wall', '-Wextra', '-Werror']
            flags += ['--target=x86_64-unknown-linux-gnu'] if cc == 'clang' else ['-m64']
            runner.command(label + '-ice-syntax', [*flags, '-fsyntax-only', src / 'ice.c'])
            runner.command(label + '-ice-object', [*flags, '-c', src / 'ice.c', '-o', objects / (label + '-ice.o')])
            runner.reject(label + '-wrong-ICE', [*flags, '-fsyntax-only', src / 'wrong.c'], 'M8_wrong_expected_value')
            runner.reject(label + '-non-ICE', [*flags, '-fsyntax-only', src / 'nonice.c'], 'M8_not_an_ICE')
            ready = True
            for unit in ['static', 'runtime', 'caller', 'same']:
                ready = runner.command(label + '-' + unit, [*flags, '-c', src / (unit + '.c'), '-o', objects / (label + '-' + unit + '.o')]) and ready
            if ready:
                exe = objects / (label + '-same.exe')
                if runner.command(label + '-link', ['clang', '-no-pie', objects / 'observer.o', objects / (label + '-static.o'), objects / (label + '-same.o'), '-o', exe]):
                    runner.execute(label + '-execute', exe, [0, 1])


def buster(root, src, runner, ide):
    prior = json.loads((root / 'reference.json').read_text())
    if prior['failed']:
        raise RuntimeError('Independent reference gate did not pass')
    objects = root / 'objects'
    (root / 'buster-sha256.txt').write_text(hashlib.sha256(ide.read_bytes()).hexdigest() + '\n')
    for ssa in ['ssa', 'memory']:
        for mode in ['none', 'mir-stack', 'fast', 'quality']:
            label = ssa + '-' + mode
            flags = [ide, 'cc', *COMMON, '-nostdinc', '-target', 'x86_64-unknown-linux-gnu',
                     '-ffrontend-ssa' if ssa == 'ssa' else '-fno-frontend-ssa',
                     '-fregister-allocator=' + mode, '-fverify-codegen', '-v']
            if mode != 'none':
                flags.append('-fno-machine-fallback')
            runner.command(label + '-ice-syntax', [*flags, '-fsyntax-only', src / 'ice.c'])
            runner.command(label + '-ice-object', [*flags, '-c', src / 'ice.c', '-o', objects / (label + '-ice.o')])
            runner.reject(label + '-wrong-ICE', [*flags, '-fsyntax-only', src / 'wrong.c'], 'M8_wrong_expected_value')
            runner.reject(label + '-non-ICE', [*flags, '-fsyntax-only', src / 'nonice.c'], 'M8_not_an_ICE')
            ready = {}
            for unit in ['static', 'runtime', 'caller', 'same']:
                obj = objects / (label + '-' + unit + '.o')
                ready[unit] = runner.command(label + '-' + unit, [*flags, '-c', src / (unit + '.c'), '-o', obj])
                if ready[unit] and unit in ['static', 'runtime']:
                    runner.command(label + '-inspect-' + unit, ['objdump', '-drs', obj])
            if ready['static'] and ready['same']:
                exe = objects / (label + '-same.exe')
                if runner.command(label + '-same-link', ['clang', '-no-pie', objects / 'observer.o', objects / (label + '-static.o'), objects / (label + '-same.o'), '-o', exe]):
                    runner.execute(label + '-same-execute', exe, [0, 1])
            for cc in ['clang', 'gcc']:
                for direction in ['buster-caller', 'buster-callee']:
                    native_unit = 'caller' if direction == 'buster-caller' else 'runtime'
                    other_unit = 'runtime' if direction == 'buster-caller' else 'caller'
                    if ready[native_unit]:
                        name = label + '-' + cc + '-' + direction
                        exe = objects / (name + '.exe')
                        if runner.command(name + '-link', ['clang', '-no-pie', objects / 'observer-runtime.o',
                                                          objects / (label + '-' + native_unit + '.o'),
                                                          objects / (cc + '-O2-' + other_unit + '.o'), '-o', exe]):
                            runner.execute(name + '-execute', exe, [1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=['reference', 'buster'])
    parser.add_argument('output', type=Path)
    parser.add_argument('--ide', type=Path)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    src = generate(root) if args.phase == 'reference' else root / 'sources'
    runner = Runner(root, args.phase)
    if args.phase == 'reference':
        external(root, src, runner)
    else:
        if args.ide is None:
            parser.error('--ide is required for buster')
        buster(root, src, runner, args.ide.resolve())
    return runner.finish()


if __name__ == '__main__':
    sys.exit(main())
