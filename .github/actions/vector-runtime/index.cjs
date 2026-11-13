// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// JavaScript is required here: the runner supplies artifact-service credentials to Node actions, not shell steps.
// The shell child inherits them only in memory. No credential is written to GITHUB_ENV, an output or a file.
// This is original protocol glue, not copied toolkit/runner implementation. Contracts consulted:
// https://github.com/actions/runner/blob/8cfa1049f86781d141056082d73802d56cd2b433/src/Runner.Worker/Handlers/NodeScriptActionHandler.cs
// https://github.com/actions/toolkit/blob/0c3a9c332b678ff1e776f93d728a4ad279803abe/packages/artifact/src/internal/shared/util.ts
// https://github.com/actions/toolkit/blob/0c3a9c332b678ff1e776f93d728a4ad279803abe/packages/artifact/src/internal/shared/artifact-twirp-client.ts
// https://github.com/actions/toolkit/blob/0c3a9c332b678ff1e776f93d728a4ad279803abe/packages/artifact/src/generated/results/api/v1/artifact.twirp-client.ts
// https://github.com/actions/toolkit/blob/0c3a9c332b678ff1e776f93d728a4ad279803abe/packages/artifact/src/internal/find/list-artifacts.ts
// ListArtifacts has no attempt filter or pagination; keep duplicate names and every attempt (latest=false).
'use strict';

const {spawnSync} = require('node:child_process');

class RuntimeError extends Error {}

function runtime_request(env)
{
    const token = env.ACTIONS_RUNTIME_TOKEN;
    let endpoint;
    let claims;
    try
    {
        endpoint = new URL(env.ACTIONS_RESULTS_URL);
        if (endpoint.protocol !== 'https:' || endpoint.username !== '' || endpoint.password !== '')
        {
            throw new Error();
        }
        const parts = typeof token === 'string' ? token.split('.') : [];
        if (parts.length !== 3 || parts.some(part => !/^[A-Za-z0-9_-]+$/.test(part)))
        {
            throw new Error();
        }
        const payload = Buffer.from(parts[1], 'base64url');
        if (payload.toString('base64url') !== parts[1])
        {
            throw new Error();
        }
        claims = JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(payload));
    }
    catch
    {
        throw new RuntimeError('Missing or invalid Actions artifact runtime credentials.');
    }
    const scopes = typeof claims?.scp === 'string' ? claims.scp.split(/\s+/).filter(scope => scope.split(':')[0] === 'Actions.Results') : [];
    const uuid = '[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}';
    const binding = scopes.length === 1 ? new RegExp(`^Actions\\.Results:(${uuid}):(${uuid})$`).exec(scopes[0]) : null;
    if (binding === null)
    {
        throw new RuntimeError('The Actions runtime token has no unambiguous Results run and job binding.');
    }
    // These are the proto field names, not the toolkit's camelCase TypeScript properties. The server verifies the JWT.
    return {
        url: new URL('/twirp/github.actions.results.api.v1.ArtifactService/ListArtifacts', endpoint.origin).href,
        headers: {'Content-Type': 'application/json', Authorization: `Bearer ${token}`},
        body: JSON.stringify({workflow_run_backend_id: binding[1], workflow_job_run_backend_id: binding[2]})
    };
}

async function list_artifacts(env = process.env, fetch_request = globalThis.fetch, timeout_signal = AbortSignal.timeout)
{
    const request = runtime_request(env);
    let response;
    try
    {
        response = await fetch_request(request.url, {
            method: 'POST',
            headers: request.headers,
            body: request.body,
            redirect: 'error',
            signal: timeout_signal(20000)
        });
    }
    catch
    {
        throw new RuntimeError('Results artifact listing failed or timed out; no REST fallback was attempted.');
    }
    if (!response.ok)
    {
        throw new RuntimeError('Results artifact listing returned an unsuccessful HTTP status.');
    }
    let body;
    try
    {
        body = await response.json();
    }
    catch
    {
        throw new RuntimeError('Results artifact listing returned invalid or incomplete JSON.');
    }
    if (body === null || typeof body !== 'object' || Array.isArray(body) || Object.keys(body).some(key => key !== 'artifacts'))
    {
        throw new RuntimeError('Results artifact listing has an unsupported response schema.');
    }
    // An omitted repeated proto field is its empty default. Refuse other schemas, including future pagination,
    // rather than treating a partial listing as a complete assignment snapshot.
    const artifacts = Object.hasOwn(body, 'artifacts') ? body.artifacts : [];
    if (!Array.isArray(artifacts))
    {
        throw new RuntimeError('Results artifact listing is missing its artifact array.');
    }
    // A conservative completeness bound, using the toolkit's default REST-list safeguard, not a promised Results cap.
    // https://github.com/actions/toolkit/blob/0c3a9c332b678ff1e776f93d728a4ad279803abe/packages/artifact/src/internal/shared/config.ts
    if (artifacts.length >= 1000)
    {
        throw new RuntimeError('Results artifact listing reached the 1000-artifact completeness bound.');
    }
    const names = [];
    for (const artifact of artifacts)
    {
        if (artifact === null || typeof artifact !== 'object' || Array.isArray(artifact) || typeof artifact.name !== 'string' || artifact.name.length === 0 || /[\0\r\n]/.test(artifact.name))
        {
            throw new RuntimeError('Results artifact listing contains an invalid artifact name.');
        }
        names.push(artifact.name);
    }
    return names;
}

function run_script(env = process.env, spawn = spawnSync)
{
    const script = env.INPUT_SCRIPT;
    if (typeof script !== 'string' || script.trim() === '')
    {
        throw new RuntimeError('The vector runtime action needs a static script input.');
    }
    const child_env = {...env, VECTOR_ARTIFACT_NODE: process.execPath, VECTOR_ARTIFACT_HELPER: __filename};
    const result = spawn('bash', ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', script], {env: child_env, stdio: 'inherit'});
    if (result.error || result.signal || !Number.isInteger(result.status))
    {
        throw new RuntimeError('The vector runtime shell could not complete.');
    }
    return result.status;
}

module.exports = {list_artifacts, run_script};

if (require.main === module)
{
    (async () =>
    {
        if (process.argv.length === 3 && process.argv[2] === 'list')
        {
            const names = await list_artifacts();
            // Write only after the complete response validates: command substitution must never receive a partial list.
            process.stdout.write(names.length === 0 ? '' : `${names.join('\n')}\n`);
        }
        else if (process.argv.length === 2)
        {
            process.exitCode = run_script();
        }
        else
        {
            throw new RuntimeError('The vector runtime helper accepts only the list command.');
        }
    })().catch(error =>
    {
        const message = error instanceof RuntimeError ? error.message : 'The vector runtime action failed.';
        process.stderr.write(`::error title=Vector runtime::${message}\n`);
        process.exitCode = 1;
    });
}
