# GitHub Pages

[Home](../README.md) · [Project catalogue](../PROJECTS.md) · [Action pins](ci-action-pins.md)

Buster's Pages integration publishes a small, static entry point for the whole
monorepo. `site/index.html` and `site/styles.css` are the complete published
payload. Project descriptions link to the canonical guides on GitHub; this is
not a generated copy of the documentation, a browser compiler, a release download
service, or a second project/status registry.

There is no site build, package installation, JavaScript, theme, external font,
analytics, or new production dependency. Python's standard library is used only
for validation and optional local preview. GitHub's deployment actions use their
own Node runtime on the hosted runner, not a Buster dependency.

## First activation

A committed workflow is not an enabled or successfully deployed Pages site.
The intended default address, after activation, is
<https://buster14a.github.io/buster/>. Track actual activation and deployment
evidence on [#2436](https://github.com/buster14a/buster/issues/2436), not here.

A repository administrator must select **Settings → Pages → Build and deployment
→ Source: GitHub Actions**. Preserve any existing domain configuration; this
integration neither sets a custom domain nor changes DNS. Restrict the
`github-pages` environment to deployments from `main` in **Settings →
Environments**, preserving any required approval policy.

After the implementation is on `main` and Pages is enabled, run **Actions →
GitHub Pages → Run workflow**, selecting `main`. Confirm that both the validation
and deployment jobs succeed and open the URL returned by the deployment. Check
that the page and stylesheet load under `/buster/` and navigation works. Until
that verification, do not advertise the intended address as a live site or close
the activation issue. A 404 from the deployment API may mean Pages is not enabled;
the workflow does not create administrative settings or silently suppress errors.

## Editing and local validation

Edit `site/` directly and keep capability statements aligned with `PROJECTS.md`
and its linked guides. Do not copy live issue status, benchmark numbers, or a
license claim into the site without its owning evidence. Buster's first-party
license remains a separate decision; the linked license records are authoritative.

From a complete repository checkout:

```sh
python3 tools/pages_test.py -v
python3 tools/check_action_pins.py .github/workflows/pages.yml
python3 tools/ci_workflow_policy_test.py
```

For preview, open `site/index.html` directly, or run:

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory site
```

The HTML uses relative local links, so the same bytes work at the domain root
and under a project prefix, without `baseurl` substitution. The offline suite
checks root, `/buster/`, and a renamed project prefix; local fragment targets;
repository document paths; semantic entry-point structure; the exact published
file list; and deployment-workflow/pin contracts. Negative controls reject root-
absolute or escaping local links, active HTML, duplicate IDs, extra published
files, symlinks, hardlinks, and unapproved Pages action revisions. These are
focused regression checks, not a general HTML/CSS security sanitizer, YAML parser,
browser accessibility audit, external link availability monitor, or live Pages
acceptance. GitHub CI's existing actionlint remains the YAML/workflow validator.

## Deployment boundary

`.github/workflows/pages.yml` validates matching pull requests and `main` pushes,
merge groups, and manual requests on a GitHub-hosted Ubuntu runner. It uploads
only the two reviewed `site/` files as a one-day `github-pages` artifact, after
validation. Documentation/catalogue changes also trigger link validation. No
compiler, benchmark, self-hosted runner, laptop, or physical performance host is
used by this workflow. Existing compiler CI and required merge checks are unchanged.

Validation has only `contents: read`, and checkout does not persist credentials.
The separate deployment job depends on successful validation and only runs for
`push` or `workflow_dispatch` on **this repository's `main`**. It has
`pages: write` and `id-token: write`, the `github-pages` environment, and only the
pinned official deployment action: no checkout or candidate scripts. Pull requests,
forks, merge groups, tags, and non-main manual requests cannot enter that job.
No personal access token, inherited secret, `pull_request_target`, or `gh-pages`
branch writer is used. PR artifacts are validation outputs, not public previews.

The Pages-only concurrency namespace separates PR numbers from branch refs.
Main publication is serialized across push and manual events, without cancelling
an in-progress deployment. GitHub may replace pending runs; this is not a promise
that every commit is published or that requests run in FIFO order. After an old
manual rerun, redeploy current `main` to restore the latest site.

Official action versions, immutable revisions, runtime/permission behavior and
license provenance are in [the action-pin guide](ci-action-pins.md). Files are
validated before the upload action dereferences paths. Expanding the two-file
payload requires an intentional test/allowlist update.

## Rollback and removal

Revert the relevant source change through the normal PR/integration process,
then deploy current `main`. Do not force-push another session's branch or edit
native-retirement generated state. To remove the published site, an administrator
must unpublish it in Pages settings; disabling the workflow alone does not remove
the last published site. No Pages acceptance result replaces compiler or merge-
queue acceptance.

## Workflow-start failures and queue recovery

A red Pages run with no jobs can fail before site validation starts. Read the
run summary's **Annotations** before attributing it to YAML or site content.
For `Queue is full for concurrency group 'pages-refs/heads/main'`, inspect the
oldest active Pages run and its deployment job, then inventory pending Pages
runs. The non-cancelling `queue: max` group retains at most 100 pending runs;
an environment wait can hold the active workflow slot until that queue fills.
Passing workflow lint does not prove that GitHub admitted a run.

Check the live `github-pages` environment's reviewers, wait timer, custom
rules and main-branch restriction. A configured approval needs its intended
reviewer; never remove or bypass a protection to drain the queue. A persisted
approval wait with no matching current rule requires separate investigation.
Job `timeout-minutes` does not bound time waiting for environment approval.

Retain the blocked run's SHA, attempt, job states, exact annotation and current
environment rule read-back on the owning issue. If the blocked deployment is
obsolete or its one-day artifact has expired, cancel that specific run through
its normal **Cancel workflow** control. Inspect the remaining queue afterwards;
do not assume cancellation preserved, completed or retried every pending run.
Do not replay an expired artifact or blindly rerun every historical failure.

After the obstruction is cleared, dispatch **GitHub Pages** on current `main`.
Record the new immutable SHA/run/attempt, successful validation and action-pin
checks, uploaded artifact and actual deployment. Confirm the public page and
stylesheet, and check that no stale deployment or renewed queue obstruction
remains. PR/merge-group validation and the trusted no-code classification keep
their existing behavior; only main push/manual events can deploy.

Incident and recovery evidence: [#3281](https://github.com/buster14a/buster/issues/3281).
This is an operational diagnostic, not a local reproduction of GitHub's
concurrency or environment scheduler.
