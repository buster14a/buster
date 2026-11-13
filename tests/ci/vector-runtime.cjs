// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The vector action's runtime credentials, Results wire contract and failure behavior, without network or real tokens.
'use strict';

const assert = require('node:assert/strict');
const {list_artifacts, run_script} = require('../../.github/actions/vector-runtime/index.cjs');

const run_id = '11111111-2222-4333-8444-555555555555';
const job_id = '66666666-7777-5888-9999-aaaaaaaaaaaa';
const scope = `Actions.Results:${run_id}:${job_id}`;
function token(claims)
{
    return `${Buffer.from('{"alg":"HS256"}').toString('base64url')}.${Buffer.from(JSON.stringify(claims)).toString('base64url')}.c2ln`;
}
const env = {
    ACTIONS_RESULTS_URL: 'https://results.example.invalid/unused/path?unused=value',
    ACTIONS_RUNTIME_TOKEN: token({scp: `Actions.ExampleScope ${scope}`})
};
let checks = 0;
async function check(test)
{
    await test();
    checks += 1;
}
function reply(body)
{
    return async () => ({ok: true, json: async () => body});
}

(async () =>
{
    await check(async () =>
    {
        const names = ['vector-O3-1-assign-1-n1x3', 'vector-O3-2-assign-1-n1x5', 'duplicate', 'duplicate'];
        let calls = 0;
        let timeouts = 0;
        const result = await list_artifacts(env, async (url, request) =>
        {
            calls += 1;
            assert.equal(url, 'https://results.example.invalid/twirp/github.actions.results.api.v1.ArtifactService/ListArtifacts');
            assert.equal(request.method, 'POST');
            assert.equal(request.redirect, 'error');
            assert.deepEqual(request.headers, {'Content-Type': 'application/json', Authorization: `Bearer ${env.ACTIONS_RUNTIME_TOKEN}`});
            assert.deepEqual(JSON.parse(request.body), {workflow_run_backend_id: run_id, workflow_job_run_backend_id: job_id});
            assert.ok(request.signal instanceof AbortSignal);
            return {ok: true, json: async () => ({artifacts: names.map(name => ({name}))})};
        }, milliseconds =>
        {
            timeouts += 1;
            assert.equal(milliseconds, 20000);
            return new AbortController().signal;
        });
        assert.deepEqual(result, names, 'All attempts and duplicate names survive the one listing.');
        assert.equal(calls, 1);
        assert.equal(timeouts, 1);
    });
    for (const body of [{}, {artifacts: []}])
    {
        await check(async () => assert.deepEqual(await list_artifacts(env, reply(body)), []));
    }
    const invalid_bodies = [
        null, [], 3, 'artifacts', {error: 'denied'}, {artifacts: null}, {artifacts: {}},
        {artifacts: [], continuation_token: 'more'}, {artifacts: [], truncated: true},
        {artifacts: [null]}, {artifacts: ['name']}, {artifacts: [[]]}, {artifacts: [{}]},
        {artifacts: [{name: ''}]}, {artifacts: [{name: 12}]}, {artifacts: [{name: 'name\nother'}]},
        {artifacts: [{name: 'name\rother'}]}, {artifacts: [{name: 'name\0other'}]},
        {artifacts: [{name: 'valid'}, {name: ''}]}
    ];
    for (const body of invalid_bodies)
    {
        await check(async () => assert.rejects(list_artifacts(env, reply(body)), /listing/));
    }
    await check(async () =>
    {
        const artifacts = Array.from({length: 999}, (_, index) => ({name: `artifact-${index}`}));
        assert.equal((await list_artifacts(env, reply({artifacts}))).length, 999);
        artifacts.push({name: 'boundary'});
        await assert.rejects(list_artifacts(env, reply({artifacts})), /completeness bound/);
        artifacts.push({name: 'past-boundary'});
        await assert.rejects(list_artifacts(env, reply({artifacts})), /completeness bound/);
    });
    const invalid_environments = [
        {}, {...env, ACTIONS_RUNTIME_TOKEN: ''}, {...env, ACTIONS_RUNTIME_TOKEN: 'invalid'},
        {...env, ACTIONS_RESULTS_URL: 'invalid'}, {...env, ACTIONS_RESULTS_URL: 'http://results.example.invalid'},
        {...env, ACTIONS_RESULTS_URL: 'https://user:secret@results.example.invalid'},
        {...env, ACTIONS_RUNTIME_TOKEN: token({})}, {...env, ACTIONS_RUNTIME_TOKEN: token({scp: [scope]})},
        {...env, ACTIONS_RUNTIME_TOKEN: token({scp: `Actions.Results:not-a-uuid:${job_id}`})},
        {...env, ACTIONS_RUNTIME_TOKEN: token({scp: `Actions.Results:${run_id}`})},
        {...env, ACTIONS_RUNTIME_TOKEN: token({scp: `${scope} ${scope}`})},
        {...env, ACTIONS_RUNTIME_TOKEN: token({scp: 'Actions.OtherScope'})}
    ];
    for (const invalid_env of invalid_environments)
    {
        await check(async () =>
        {
            let calls = 0;
            await assert.rejects(list_artifacts(invalid_env, async () => { calls += 1; }), /credentials|binding/);
            assert.equal(calls, 0, 'Bad runtime bindings never make a request.');
        });
    }
    await check(async () =>
    {
        let calls = 0;
        await assert.rejects(list_artifacts(env, async () =>
        {
            calls += 1;
            throw new Error(env.ACTIONS_RUNTIME_TOKEN);
        }), error => !error.message.includes(env.ACTIONS_RUNTIME_TOKEN) && /failed or timed out/.test(error.message));
        assert.equal(calls, 1, 'The outer draw owns retries.');
    });
    await check(async () =>
    {
        let reads = 0;
        await assert.rejects(list_artifacts(env, async () => ({
            ok: false,
            status: 403,
            json: async () => { reads += 1; return {secret: env.ACTIONS_RUNTIME_TOKEN}; }
        })), /unsuccessful HTTP status/);
        assert.equal(reads, 0, 'An HTTP error response cannot leak its body into a diagnostic.');
    });
    await check(async () =>
    {
        await assert.rejects(list_artifacts(env, async () => ({
            ok: true,
            json: async () => { throw new SyntaxError(env.ACTIONS_RUNTIME_TOKEN); }
        })), error => !error.message.includes(env.ACTIONS_RUNTIME_TOKEN) && /invalid or incomplete JSON/.test(error.message));
    });
    await check(async () =>
    {
        let calls = 0;
        const controller = new AbortController();
        await assert.rejects(list_artifacts(env, async (_url, request) =>
        {
            calls += 1;
            return new Promise((_resolve, reject) =>
            {
                request.signal.addEventListener('abort', () => reject(new Error('fixture timeout')), {once: true});
                queueMicrotask(() => controller.abort());
            });
        }, milliseconds =>
        {
            assert.equal(milliseconds, 20000);
            return controller.signal;
        }), /timed out/);
        assert.equal(calls, 1);
    });
    await check(async () =>
    {
        const script = 'printf \'%s\\n\' "$BUSTER_SANITIZER"\nexit 17\n';
        const parent_env = {...env, INPUT_SCRIPT: script, GITHUB_ENV: '/fixture/env', GITHUB_OUTPUT: '/fixture/output', BUSTER_SANITIZER: 'thread'};
        const before = {...parent_env};
        const status = run_script(parent_env, (command, args, options) =>
        {
            assert.equal(command, 'bash');
            assert.deepEqual(args, ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', script]);
            assert.equal(options.stdio, 'inherit');
            assert.equal(options.env.ACTIONS_RUNTIME_TOKEN, env.ACTIONS_RUNTIME_TOKEN);
            assert.equal(options.env.ACTIONS_RESULTS_URL, env.ACTIONS_RESULTS_URL);
            assert.equal(options.env.GITHUB_ENV, '/fixture/env');
            assert.equal(options.env.GITHUB_OUTPUT, '/fixture/output');
            assert.equal(options.env.BUSTER_SANITIZER, 'thread');
            assert.equal(options.env.VECTOR_ARTIFACT_NODE, process.execPath);
            assert.equal(options.env.VECTOR_ARTIFACT_HELPER, require.resolve('../../.github/actions/vector-runtime/index.cjs'));
            return {status: 17};
        });
        assert.equal(status, 17, 'The action returns the shell failure unchanged.');
        assert.deepEqual(parent_env, before, 'Runtime variables are private to the child, not exported to later steps.');
    });
    await check(async () =>
    {
        assert.throws(() => run_script({INPUT_SCRIPT: '  '}), /static script input/);
        for (const result of [{status: null}, {status: null, signal: 'SIGTERM'}, {error: new Error(env.ACTIONS_RUNTIME_TOKEN)}])
        {
            assert.throws(() => run_script({INPUT_SCRIPT: 'true'}, () => result), error => !error.message.includes(env.ACTIONS_RUNTIME_TOKEN) && /could not complete/.test(error.message));
        }
    });
    process.stdout.write(`vector-runtime fixture: ${checks} checks passed\n`);
})().catch(error =>
{
    process.stderr.write(`vector-runtime fixture: ${error.stack}\n`);
    process.exitCode = 1;
});
