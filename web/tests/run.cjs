// Browser automation requires the browser's JavaScript API. Receipts are
// written outside Buster when BUSTER_BROWSER_RECEIPT is provided.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
const {chromium} = require('playwright');
const fs = require('node:fs');

(async () =>
{
    const browser = await chromium.launch({headless: true, args: ['--no-sandbox']});
    try
    {
        const page = await browser.newPage();
        const failures = [];
        page.on('pageerror', error => failures.push(error.message));
        await page.goto((process.env.BUSTER_BROWSER_BASE || 'http://127.0.0.1:8765') + '/buster/tests/reader.html');
        await page.waitForFunction(() => window.__browserResult !== undefined);
        const result = await page.evaluate(() => window.__browserResult);
        result.browserVersion = browser.version();
        result.pageErrors = failures;
        if (process.env.BUSTER_BROWSER_RECEIPT) fs.writeFileSync(process.env.BUSTER_BROWSER_RECEIPT, JSON.stringify(result, null, 2) + '\n');
        console.log(JSON.stringify(result));
        if (!result.passed || failures.length) process.exitCode = 1;
    }
    finally
    {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
