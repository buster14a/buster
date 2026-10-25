/* Original native-profile worker. Fixed same-origin code; no remote plugins.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
import {parseTrace, queryProfile} from './trace.mjs';
import {sha256} from './types.mjs';
let trace;
let identity;
self.onmessage = async event =>
{
    const request = event.data;
    try
    {
        if (!Number.isSafeInteger(request.id)) throw new Error('Invalid profile request identity');
        const started = performance.now();
        if (request.type === 'load')
        {
            if (!(request.bytes instanceof ArrayBuffer) || !/^[0-9a-f]{64}$/u.test(request.sha256)) throw new Error('Invalid profile resource');
            if (await sha256(new Uint8Array(request.bytes)) !== request.sha256) throw new Error('Native profile checksum differs');
            trace = parseTrace(new Uint8Array(request.bytes));
            identity = request.sha256;
            self.postMessage({id: request.id, type: 'loaded', identity, start: trace.start, end: trace.end, primaryClock: trace.primaryClock, warnings: trace.warnings, capabilities: trace.capabilities, sampleCount: trace.samples.length, lost: trace.lost, skipped: trace.skipped, tracks: trace.tracks, threads: trace.threads, elapsed_ms: performance.now() - started});
        }
        else if (request.type === 'range')
        {
            if (!trace || request.identity !== identity) throw new Error('Stale profile identity');
            const result = queryProfile(trace, BigInt(request.start), BigInt(request.end), request.filters ?? {});
            self.postMessage({id: request.id, type: 'range', identity, result, elapsed_ms: performance.now() - started});
        }
        else throw new Error('Unknown profile operation');
    }
    catch (error) { self.postMessage({id: request?.id, type: 'error', message: error.message}); }
};
