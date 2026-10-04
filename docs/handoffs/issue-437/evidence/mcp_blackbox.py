#!/usr/bin/env python3
"""Independent bounded MCP stdio checks; never opens a real service socket."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import time
from pathlib import Path

PARSER_LIMIT = 16384
SAFE_INTEGER = 9007199254740991
MAX_U64 = 18446744073709551615
VERSIONS = ('2025-11-25', '2025-06-18')
CHECKS = []
OBSERVATIONS = []
ALL_RUNS = []


def compact(obj):
    return json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode('utf-8')


def request(method, ident=1, params=None):
    obj = {'jsonrpc': '2.0', 'id': ident, 'method': method}
    if params is not None:
        obj['params'] = params
    return obj


def init(version=VERSIONS[0], ident=1):
    return request('initialize', ident, {'protocolVersion': version, 'capabilities': {}, 'clientInfo': {'name': 'independent-blackbox', 'version': '1'}})


def notification(method, params=None):
    obj = {'jsonrpc': '2.0', 'method': method}
    if params is not None:
        obj['params'] = params
    return obj


def ready_prefix():
    return [compact(init(ident='initialize-blackbox')), compact(notification('notifications/initialized'))]


def call(name, args, ident=2, meta=None):
    params = {'name': name, 'arguments': args}
    if meta is not None:
        params['_meta'] = meta
    return request('tools/call', ident, params)


def record(name, ok, detail=None):
    CHECKS.append({'name': name, 'passed': bool(ok), 'detail': detail})


def rpc_shape(obj):
    if not isinstance(obj, dict) or obj.get('jsonrpc') != '2.0':
        return False
    if ('result' in obj) == ('error' in obj):
        return False
    if 'error' in obj:
        e = obj['error']
        return isinstance(e, dict) and isinstance(e.get('code'), int) and not isinstance(e.get('code'), bool) and isinstance(e.get('message'), str)
    return isinstance(obj.get('result'), dict) and 'id' in obj


def run(name, lines, *, terminate_line=True, expect_exit=0):
    chunks = [compact(line) if isinstance(line, dict) else line for line in lines]
    payload = b'\n'.join(chunks) + (b'\n' if terminate_line and chunks else b'')
    t0 = time.monotonic()
    proc = subprocess.Popen([BINARY, 'mcp', MISSING_SOCKET], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout, stderr = proc.communicate(payload, timeout=4)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()
        record(name + '/termination', False, 'Exceeded 4-second local protocol deadline')
    duration = time.monotonic() - t0
    parsed = []
    bad_output = []
    try:
        text = stdout.decode('utf-8', errors='strict')
        for line in text.splitlines():
            try:
                parsed.append(json.loads(line))
            except ValueError:
                bad_output.append(line[:200])
    except UnicodeDecodeError:
        bad_output.append('stdout is not UTF-8')
    record(name + '/stdout', not bad_output and (not stdout or stdout.endswith(b'\n')) and all(rpc_shape(o) for o in parsed), bad_output or None)
    if expect_exit == 'nonzero':
        record(name + '/exit', proc.returncode != 0, proc.returncode)
    else:
        record(name + '/exit', proc.returncode == expect_exit, proc.returncode)
    ALL_RUNS.append({'name': name, 'input_sha256': hashlib.sha256(payload).hexdigest(), 'input_bytes': len(payload), 'stdout_bytes': len(stdout), 'responses': parsed, 'exit_code': proc.returncode, 'stderr': stderr.decode('utf-8', 'replace'), 'elapsed_seconds': round(duration, 6)})
    return parsed


def error_matches(obj, code=None, ident=None):
    return isinstance(obj, dict) and 'error' in obj and (code is None or obj['error']['code'] == code) and (ident is None or obj.get('id') == ident)


def batch_one(name, msg, expected, *, prefix=True):
    lines = (ready_prefix() if prefix else []) + [msg, request('ping', 'still-alive')]
    out = run(name, lines)
    offset = 1 if prefix else 0
    record(name + '/response_count', len(out) == offset + 2, len(out))
    if len(out) >= offset + 2:
        record(name + '/response', expected(out[offset]), out[offset])
        record(name + '/survived', out[offset + 1].get('id') == 'still-alive' and out[offset + 1].get('result') == {}, out[offset + 1])
    return out[offset] if len(out) > offset else None


def simple_schema_valid(value, schema):
    st = schema.get('type')
    if st == 'object':
        if not isinstance(value, dict):
            return False
        props = schema.get('properties', {})
        if any(k not in value for k in schema.get('required', [])):
            return False
        if schema.get('additionalProperties') is False and any(k not in props for k in value):
            return False
        return all(k not in props or simple_schema_valid(v, props[k]) for k, v in value.items())
    if st == 'string':
        if not isinstance(value, str):
            return False
        return len(value) >= schema.get('minLength', 0) and len(value) <= schema.get('maxLength', len(value)) and ('pattern' not in schema or re.search(schema['pattern'], value) is not None) and ('enum' not in schema or value in schema['enum'])
    return True


def test_lifecycle():
    for version in VERSIONS + ('2099-01-01', '2024-11-05'):
        out = run('version/' + version, [init(version), notification('notifications/initialized'), request('ping', 2), request('tools/list', 3)])
        expected = version if version in VERSIONS else VERSIONS[0]
        record('version/' + version + '/negotiated', len(out) == 3 and out[0].get('result', {}).get('protocolVersion') == expected, out)
        if out:
            record('version/' + version + '/capabilities', out[0].get('result', {}).get('capabilities', {}).get('tools') == {}, out[0])
    batch_one('lifecycle/preinitialize_list', request('tools/list'), lambda o: error_matches(o, -32002), prefix=False)
    out = run('lifecycle/no_initialized', [init(), request('tools/list', 2), request('ping', 3)])
    record('lifecycle/no_initialized/blocked', len(out) == 3 and error_matches(out[1], -32002, 2), out)
    batch_one('lifecycle/reinitialize', init(ident=50), lambda o: error_matches(o, -32602, 50))
    for label, params in [('array', []), ('string', 'bad'), ('number', 1), ('meta_array', {'_meta': []}), ('meta_token_null', {'_meta': {'progressToken': None}})]:
        out = run('lifecycle/invalid_initialized_' + label, [init(), notification('notifications/initialized', params), request('tools/list', 2)])
        record('lifecycle/invalid_initialized_' + label + '/does_not_advance', bool(out) and error_matches(out[-1], -32002, 2), out)
    for value in [[], 1, 'bad', True, {'_meta': []}, {'_meta': {'progressToken': None}}, {'_meta': {'progressToken': True}}, {'_meta': {'progressToken': []}}]:
        batch_one('params/ping/' + type(value).__name__, request('ping', 100, value), lambda o: error_matches(o))
    batch_one('method/unknown', request('nonexistent', 51), lambda o: error_matches(o, -32601, 51))
    batch_one('tools/list_unknown_cursor', request('tools/list', 2, {'cursor': 'opaque'}), lambda o: error_matches(o, -32602, 2))
    batch_one('tools/list_meta', request('tools/list', 2, {'_meta': {'progressToken': 'x'}}), lambda o: isinstance(o.get('result', {}).get('tools'), list))
    for method in ('ping', 'tools/list'):
        msg = request(method, 2)
        msg['params'] = None
        batch_one('params/null/' + method, msg, lambda o: error_matches(o))
    for meta in (None, [], {'progressToken': None}, {'progressToken': True}, {'progressToken': []}):
        msg = init(ident=2)
        msg['params']['_meta'] = meta
        batch_one('initialize/meta_invalid/' + repr(meta), msg, lambda o: error_matches(o), prefix=False)
    for number in (0, 1, -1, SAFE_INTEGER, -SAFE_INTEGER):
        batch_one('id/numeric/' + str(number), request('ping', number), lambda o, n=number: o.get('id') == n and o.get('result') == {})
    for number in (SAFE_INTEGER + 1, -(SAFE_INTEGER + 1), MAX_U64, MAX_U64 + 1, 1.25, None, True, {}, []):
        batch_one('id/reject/' + repr(number), request('ping', number), lambda o: error_matches(o, -32600))
    for value in ('', 'ascii', 'x' * 256, 'é' * 128, '\"\\\n\r\t\u0000', '你好🙂', '18446744073709551615'):
        batch_one('id/string/' + hashlib.sha256(value.encode()).hexdigest()[:10], request('ping', value), lambda o, v=value: o.get('id') == v and o.get('result') == {})
    for value in ('x' * 257, 'é' * 129):
        batch_one('id/string_reject/' + str(len(value.encode())), request('ping', value), lambda o: error_matches(o, -32600))


def test_tools():
    out = run('tools/discovery', ready_prefix() + [request('tools/list', 2)])
    tools = out[-1].get('result', {}).get('tools', []) if out else []
    expected_names = ['bench_capabilities', 'bench_submit', 'bench_status', 'bench_result', 'bench_cancel', 'bench_logs']
    record('tools/discovery/names', [t.get('name') for t in tools] == expected_names, [t.get('name') for t in tools])
    schemas = {t['name']: t.get('inputSchema', {}) for t in tools}
    record('tools/discovery/schemas', len(schemas) == 6 and all(s.get('type') == 'object' and s.get('additionalProperties') is False and set(s.get('required', [])).issubset(s.get('properties', {})) for s in schemas.values()), schemas)
    valid_submit = {'idempotency_key': 'blackbox-on-absent-socket', 'recipe': 'validate-buster-v1', 'baseline_sha': 'a' * 40, 'candidate_sha': 'b' * 40}
    valid_calls = [('bench_capabilities', {}), ('bench_submit', valid_submit)] + [(n, {'job_id': str(MAX_U64)}) for n in ('bench_status', 'bench_result', 'bench_cancel')] + [('bench_logs', {'job_id': str(MAX_U64), 'cursor': str(MAX_U64)})]
    for name, args in valid_calls:
        record('schema/' + name + '/valid', simple_schema_valid(args, schemas.get(name, {})), args)
        response = batch_one('tool/' + name + '/absent_socket', call(name, args), lambda o: o.get('result', {}).get('isError') is True and isinstance(o.get('result', {}).get('structuredContent'), dict))
        if response and 'result' in response:
            result = response['result']
            content = result.get('content', [])
            record('tool/' + name + '/consistent_text', len(content) == 1 and content[0].get('type') == 'text' and json.loads(content[0]['text']) == result.get('structuredContent'), result)
            record('tool/' + name + '/truthful_state', result.get('structuredContent', {}).get('execution_state') == 'unknown', result)
    batch_one('tool/capabilities_no_arguments', request('tools/call', 2, {'name': 'bench_capabilities'}), lambda o: o.get('result', {}).get('isError') is True)
    batch_one('tool/meta_valid', call('bench_capabilities', {}, meta={'progressToken': 'x'}), lambda o: o.get('result', {}).get('isError') is True)
    batch_one('tool/meta_invalid', call('bench_capabilities', {}, meta=3), lambda o: error_matches(o))
    for meta in ([], {'progressToken': None}, {'progressToken': True}, {'progressToken': []}):
        batch_one('tool/meta_invalid/' + repr(meta), call('bench_capabilities', {}, meta=meta), lambda o: error_matches(o))
    for name in expected_names:
        args = {} if name == 'bench_capabilities' else valid_submit if name == 'bench_submit' else {'job_id': '1'}
        for suffix, candidate in [('extra', {**args, 'bogus': 1}), ('array', []), ('string', 'bad'), ('null', None)]:
            batch_one('tool/' + name + '/invalid_' + suffix, call(name, candidate), lambda o: error_matches(o, -32602))
    for value in ('0', '00', '01', '-1', '+1', '1.0', '1e0', '', str(MAX_U64 + 1), '9' * 21, 1, None, True):
        batch_one('tool/job_id_reject/' + repr(value), call('bench_status', {'job_id': value}), lambda o: error_matches(o, -32602))
    for cursor in ('0', str(MAX_U64)):
        batch_one('tool/cursor_valid/' + cursor, call('bench_logs', {'job_id': '1', 'cursor': cursor}), lambda o: o.get('result', {}).get('isError') is True)
    for cursor in ('00', '-1', str(MAX_U64 + 1), 0):
        batch_one('tool/cursor_reject/' + repr(cursor), call('bench_logs', {'job_id': '1', 'cursor': cursor}), lambda o: error_matches(o, -32602))
    for field, value in [('recipe', 'unlisted'), ('baseline_sha', 'A' * 40), ('candidate_sha', 'b' * 39), ('idempotency_key', 'space key'), ('idempotency_key', ''), ('idempotency_key', 'x' * 65)]:
        batch_one('tool/submit_reject/' + field + '/' + hashlib.sha256(str(value).encode()).hexdigest()[:8], call('bench_submit', {**valid_submit, field: value}), lambda o: error_matches(o, -32602))
    batch_one('tool/submit_sha256', call('bench_submit', {**valid_submit, 'baseline_sha': 'a' * 64, 'candidate_sha': 'b' * 64}), lambda o: o.get('result', {}).get('isError') is True)
    notes = []
    for field, value in [('job_id', str(MAX_U64 + 1)), ('job_id', '9' * 20)]:
        if simple_schema_valid({field: value}, schemas.get('bench_status', {})):
            notes.append({'field': field, 'schema_valid_but_out_of_uint64_range': value})
    OBSERVATIONS.append({'name': 'schema_runtime_domain', 'details': notes})
    notifications = [notification('tools/call', {'name': n, 'arguments': a}) for n, a in valid_calls]
    notifications += [notification('notifications/cancelled', {'requestId': 2, 'reason': 'transport-only'}), notification('unknown_notification', {})]
    out = run('notifications/ignored', ready_prefix() + notifications + [request('ping', 'after-notifications')])
    record('notifications/no_responses', len(out) == 2 and out[-1].get('id') == 'after-notifications', out)
    record('notifications/no_socket_created', not Path(MISSING_SOCKET).exists())


def test_parser():
    malformed = [b'', b' ', b'{', b'[', b'nullx', b'truex', b'{}{}', b'{"jsonrpc":"2.0","id":1,"method":"ping",}', b'{"a":1,}', b'[1,]', b'{"a" 1}', b'{"a":01}', b'{"a":1.}', b'{"a":1e}', b'{"a":+1}', b'{"a":NaN}', b'{"a":"\\x"}', b'{"a":"\\u000"}', b'{"a":"\\ud800"}', b'{"a":"\\udc00"}', b'{"a":"\\ud800\\u0041"}', b'{"a":"a\x00b"}', b'{"a":"\xc0\xaf"}', b'{"a":"\xed\xa0\x80"}', b'{"a":"\xf4\x90\x80\x80"}', b'{"a":"\x80"}', b'{"a":1,"a":2}', b'{"a":1,"\\u0061":2}']
    for i, msg in enumerate(malformed):
        batch_one('parse/malformed/' + str(i), msg, lambda o: error_matches(o, -32700))
    for value in (None, True, 5, 'string', [], {}, [request('ping')]):
        batch_one('parse/nonobject_or_invalid/' + repr(value)[:25], compact(value), lambda o: error_matches(o, -32600))
    for depth in (1, 20, 31, 32, 33, 64, 256):
        msg = b'{"jsonrpc":"2.0","id":2,"method":"ping","params":' + b'[' * depth + b'0' + b']' * depth + b'}'
        expected = (lambda o: error_matches(o, -32700)) if depth >= 32 else (lambda o: rpc_shape(o))
        batch_one('parse/depth/' + str(depth), msg, expected)
    for count in (28, 29):
        nested = None
        for _ in range(count):
            nested = [nested]
        obj = init()
        obj['params']['capabilities'] = {'experimental': {'nested': nested}}
        out = run('parse/exact_depth/' + str(4 + count), [obj, request('ping', 'depth-boundary-survived')])
        record('parse/exact_depth/' + str(4 + count) + '/limit', len(out) == 2 and (('result' in out[0]) if count == 28 else error_matches(out[0], -32700)), out)
    valid_complex = {'jsonrpc': '2.0', 'id': 'unicode:你好🙂', 'method': 'initialize', 'params': {'protocolVersion': VERSIONS[0], 'capabilities': {'experimental': {'arrays': [True, False, None, {'k': 1.5e10}]}}, 'clientInfo': {'name': 'á🙂', 'version': '1'}}}
    batch_one('parse/complex_unicode', compact(valid_complex), lambda o: 'result' in o, prefix=False)
    out = run('parse/escaped_id', ready_prefix() + [b'{"jsonrpc":"2.0","id":"\\ud83d\\ude42\\u0061\\b\\f\\n\\r\\t\\/\\\\\\\"","method":"ping"}'])
    record('parse/escaped_id/roundtrip', len(out) == 2 and out[-1].get('id') == '🙂a\b\f\n\r\t/\\"', out)
    for count in (100, 248, 256, 512, 1024):
        capabilities = {'x' + str(i): None for i in range(count)}
        obj = init()
        obj['params']['capabilities'] = capabilities
        out = run('parse/token_count/' + str(count), [obj, request('ping', 'token-survived')])
        record('parse/token_count/' + str(count) + '/survived', len(out) == 2 and out[-1].get('result') == {}, out)
    for count in (489, 490):
        obj = init()
        obj['params']['capabilities'] = {'experimental': {'entries': [None] * count}}
        out = run('parse/exact_tokens/' + str(23 + count), [obj, request('ping', 'token-boundary-survived')])
        record('parse/exact_tokens/' + str(23 + count) + '/limit', len(out) == 2 and (('result' in out[0]) if count == 489 else error_matches(out[0], -32700)), out)
    base = compact(request('ping', 2))
    for size in (PARSER_LIMIT - 1, PARSER_LIMIT):
        padded = base + b' ' * (size - len(base))
        batch_one('frame/exact_bytes/' + str(size), padded, lambda o: o.get('id') == 2 and o.get('result') == {})
    for size, terminated in ((PARSER_LIMIT + 1, True), (PARSER_LIMIT + 1, False), (PARSER_LIMIT * 4, True)):
        payload = b'x' * size
        out = run('frame/oversized/' + str(size) + '/' + str(terminated), [payload, request('ping', 'must-not-run')] if terminated else [payload], terminate_line=terminated, expect_exit='nonzero')
        record('frame/oversized/' + str(size) + '/' + str(terminated) + '/single_error', len(out) == 1 and error_matches(out[0], -32700), out)
    out = run('frame/clean_eof', [])
    record('frame/clean_eof/quiet', not out)
    for msg in (base, b'{'):
        out = run('frame/unterminated/' + hashlib.sha256(msg).hexdigest()[:8], [msg], terminate_line=False, expect_exit='nonzero')
        record('frame/unterminated/error', len(out) == 1 and error_matches(out[0], -32700), out)
    for i in range(3):
        out = run('frame/reconnect/' + str(i), [init(), notification('notifications/initialized'), request('ping', i + 2)])
        record('frame/reconnect/' + str(i) + '/fresh_lifecycle', len(out) == 2 and out[-1].get('result') == {}, out)
    # Fixed-position insertion/deletion/replacement mutations are replayable.
    mutations = []
    seed = compact(request('ping', 2, {'_meta': {'x': [1, True, None]}}))
    positions = sorted(set(range(0, len(seed), max(1, len(seed) // 12))) | {len(seed) - 1})
    for position in positions:
        mutations.extend([seed[:position] + seed[position + 1:], seed[:position] + b'@' + seed[position + 1:], seed[:position] + b'\x00' + seed[position:]])
    for i, msg in enumerate(mutations):
        batch_one('fuzz/syntax/' + str(i), msg, lambda o: rpc_shape(o))


def main():
    global BINARY, MISSING_SOCKET
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--output', default='work/mcp-blackbox-results.json')
    args = parser.parse_args()
    BINARY = str(Path(args.binary).resolve())
    with tempfile.TemporaryDirectory(prefix='bq-mcp-blackbox-', dir='/tmp') as absent_dir:
        MISSING_SOCKET = str(Path(absent_dir) / 'absent.sock')
        assert len(MISSING_SOCKET.encode('utf-8')) < 108, 'Missing socket must fit sockaddr_un.sun_path'
        if Path(MISSING_SOCKET).exists():
            raise SystemExit('Refusing to use an existing service socket')
        test_lifecycle()
        test_tools()
        test_parser()
        result = {'binary': BINARY, 'binary_sha256': hashlib.sha256(Path(BINARY).read_bytes()).hexdigest(), 'service_socket': MISSING_SOCKET, 'official_sources': ['https://modelcontextprotocol.io/specification/2025-11-25/basic/transports', 'https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle', 'https://modelcontextprotocol.io/specification/2025-11-25/server/tools', 'https://modelcontextprotocol.io/specification/2025-11-25/basic'], 'runs': ALL_RUNS, 'checks': CHECKS, 'observations': OBSERVATIONS, 'summary': {'runs': len(ALL_RUNS), 'checks': len(CHECKS), 'passed': sum(c['passed'] for c in CHECKS), 'failed': sum(not c['passed'] for c in CHECKS)}}
        Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps(result['summary']))
        for check in CHECKS:
            if not check['passed']:
                print(json.dumps({**check, 'detail': str(check.get('detail'))[:500]}, ensure_ascii=False))
        print(json.dumps({'observations': OBSERVATIONS}, ensure_ascii=False))
        return 1 if result['summary']['failed'] else 0

if __name__ == '__main__':
    raise SystemExit(main())
