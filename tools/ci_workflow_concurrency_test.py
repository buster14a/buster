#!/usr/bin/env python3
"""Offline regression for retaining main-push CI during merge bursts."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MainPushConcurrencyTests(unittest.TestCase):
    SERIAL = {
        "native-retirement-automation.yml": "native-retirement-automation-controller",
        "native-retirement-catch-up.yml": "native-retirement-catch-up",
        "merge-queue-reconcile.yml": "merge-queue-admission-reconcile",
    }

    def context(self, event="push", number=101):
        return {
            "github.workflow": "Workflow under test",
            "github.event_name": event,
            "github.run_id": number,
            "github.sha": format(number, "040x"),
            "github.ref": "refs/heads/main" if event == "push" else
                          "refs/pull/7/merge" if event == "pull_request" else
                          "refs/heads/gh-readonly-queue/main/pr-7-example",
            "github.event.repository.default_branch": "main",
            "github.event.pull_request.number": 7 if event == "pull_request" else "",
            "github.event.merge_group.head_sha": format(number, "040x")
                                                  if event == "merge_group" else "",
            "github.event.workflow_run.id": "",
        }

    def expression(self, text, context):
        # Only literal scalars, known contexts, comparisons and Boolean operators
        # enter eval. Reject unrecognized syntax instead of executing workflow code.
        text = text.replace(
            "format('refs/heads/{0}', github.event.repository.default_branch)",
            repr("refs/heads/" + context["github.event.repository.default_branch"]),
        )
        token = re.compile(r"'[^'\\]*'|github\.[a-z_.]+|==|!=|&&|\|\||true|false|[0-9]+")
        translated = []
        end = 0
        for match in token.finditer(text):
            self.assertFalse(text[end:match.start()].strip(), text)
            value = match.group()
            if value.startswith("github."):
                self.assertIn(value, context)
                value = repr(context[value])
            else:
                value = {"&&": "and", "||": "or", "true": "True",
                         "false": "False"}.get(value, value)
            translated.append(value)
            end = match.end()
        self.assertFalse(text[end:].strip(), text)
        return eval(" ".join(translated), {"__builtins__": {}}, {})

    def fields(self, text):
        block = re.search(r"(?ms)^concurrency:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
        self.assertIsNotNone(block)
        return dict(line.strip().split(": ", 1) for line in block.group(1).splitlines()
                    if line.strip() and not line.lstrip().startswith("#"))

    def resolved_group(self, group, context):
        return re.sub(r"\$\{\{(.*?)\}\}",
                      lambda match: str(self.expression(match.group(1), context)),
                      group).lower()

    def cancelled(self, value, context):
        if value.startswith("$" + "{{"):
            value = value[3:-2]
        return bool(self.expression(value, context))

    def test_every_main_push_workflow_retains_three_overlapping_runs(self):
        inspected = set()
        for path in sorted((ROOT / ".github/workflows").glob("*.yml")):
            text = path.read_text()
            events = re.search(r"(?ms)^on:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
            if events is None:
                continue
            push = re.search(r"(?ms)^  push:(.*?)(?=^  [a-z_]+:|\Z)", events.group(1))
            if push is None or not re.search(r"branches:\s*\[main\]|^\s+- main\s*$",
                                             push.group(1), re.M):
                continue
            inspected.add(path.name)
            with self.subTest(workflow=path.name):
                fields = self.fields(text)
                contexts = [self.context(number=number) for number in (101, 102, 103)]
                for context in contexts:
                    self.assertFalse(self.cancelled(fields["cancel-in-progress"], context))
                groups = {self.resolved_group(fields["group"], context) for context in contexts}
                if path.name in self.SERIAL:
                    self.assertEqual(groups, {self.SERIAL[path.name]})
                    self.assertEqual(fields.get("queue"), "max")
                else:
                    self.assertEqual(len(groups), 3,
                                     "A newer main push must not replace pending validation")
        self.assertTrue(set(self.SERIAL) <= inspected)
        self.assertTrue({"bench-service-policy.yml", "broker-entry-gate-systemd.yml",
                         "credential-gate-systemd.yml"} <= inspected)
        self.assertGreaterEqual(len(inspected), 15)

    def test_benchmark_candidates_coalesce_without_cross_event_cancellation(self):
        fields = self.fields((ROOT / ".github/workflows/bench-service-policy.yml").read_text())
        event_groups = {}
        for event in ("pull_request", "merge_group", "push", "workflow_dispatch"):
            contexts = [self.context(event, number) for number in (101, 102, 103)]
            event_groups[event] = {self.resolved_group(fields["group"], c) for c in contexts}
            self.assertEqual(len(event_groups[event]), 1 if event in
                             ("pull_request", "merge_group") else 3)
            for context in contexts:
                self.assertEqual(self.cancelled(fields["cancel-in-progress"], context),
                                 event in ("pull_request", "merge_group"))
        for event, groups in event_groups.items():
            for other, other_groups in event_groups.items():
                if event != other:
                    self.assertTrue(groups.isdisjoint(other_groups))

    def test_disposable_gate_candidate_runs_remain_non_cancelling(self):
        for name in ("broker-entry-gate-systemd.yml", "credential-gate-systemd.yml"):
            fields = self.fields((ROOT / ".github/workflows" / name).read_text())
            self.assertEqual(fields["cancel-in-progress"], "false")
            self.assertNotIn("queue", fields)


    def test_auxiliary_validation_preserves_candidate_policy_and_isolates_pushes(self):
        for name, cancel_pr in (("debug-lifetime-slice.yml", False),
                                ("hot-reload-demo.yml", True)):
            fields = self.fields((ROOT / ".github/workflows" / name).read_text())
            with self.subTest(workflow=name):
                self.assertNotIn("queue", fields)
                pr = self.context("pull_request")
                pr_next = self.context("pull_request", 102)
                self.assertEqual(self.resolved_group(fields["group"], pr),
                                 self.resolved_group(fields["group"], pr_next))
                self.assertEqual(self.cancelled(fields["cancel-in-progress"], pr),
                                 cancel_pr)
                other_pr = dict(pr, **{"github.event.pull_request.number": 8,
                                     "github.ref": "refs/pull/8/merge"})
                self.assertNotEqual(self.resolved_group(fields["group"], pr),
                                    self.resolved_group(fields["group"], other_pr))
                pushes = [self.context(number=number) for number in (101, 102, 103)]
                for context in pushes:
                    context["github.sha"] = pr["github.sha"]
                    self.assertFalse(self.cancelled(fields["cancel-in-progress"], context))
                groups = {self.resolved_group(fields["group"], c) for c in pushes}
                self.assertEqual(len(groups), 3)
                self.assertNotIn(self.resolved_group(fields["group"], pr), groups)
                if name == "hot-reload-demo.yml":
                    manual = self.context("workflow_dispatch")
                    manual["github.ref"] = "refs/heads/main"
                    self.assertFalse(self.cancelled(fields["cancel-in-progress"], manual))
                    self.assertNotIn(self.resolved_group(fields["group"], manual), groups)


if __name__ == "__main__":
    unittest.main()
