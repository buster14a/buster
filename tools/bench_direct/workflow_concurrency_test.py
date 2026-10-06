#!/usr/bin/env python3
"""Stateless validation scheduling contract; see docs/ci-stateless-concurrency.md."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ("bench-service-policy",)


def scalar(token, context):
    token = token.strip()
    if token in context:
        value = context[token]
    elif token in ("true", "false"):
        value = token == "true"
    elif re.fullmatch(r"'[a-z_]+'", token):
        value = token[1:-1]
    else:
        raise AssertionError("Unsupported concurrency operand: " + token)
    return value


def expression(text, context):
    # Evaluate only the checked-in scalar/equality/AND/OR subset, without eval.
    result = False
    for alternative in text.split("||"):
        for operand in alternative.split("&&"):
            left, equality, right = operand.partition("==")
            result = scalar(left, context)
            if equality:
                result = result == scalar(right, context)
            if not result:
                break
        if result:
            break
    return result


class StatelessConcurrencyTests(unittest.TestCase):
    def context(self, event="push", run_id=101, pr=7, ref=None):
        return {
            "github.event_name": event,
            "github.run_id": run_id,
            # Duplicate invocations of one SHA must also survive.
            "github.sha": "a" * 40,
            "github.event.pull_request.number": pr if event == "pull_request" else "",
            "github.ref": ref or ("refs/heads/gh-readonly-queue/main/pr-7-example"
                                  if event == "merge_group" else "refs/heads/main"),
        }

    def fields(self, name):
        text = (ROOT / ".github/workflows" / (name + ".yml")).read_text(encoding="utf-8")
        block = re.search(r"(?ms)^concurrency:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
        self.assertIsNotNone(block)
        lines = [line.strip() for line in block.group(1).splitlines()
                 if line.strip() and not line.lstrip().startswith("#")]
        fields = dict(line.split(": ", 1) for line in lines)
        self.assertEqual(len(lines), len(fields), "Duplicate concurrency key")
        self.assertEqual(set(fields), {"group", "cancel-in-progress"})
        return fields

    def group(self, fields, context):
        return re.sub(r"\$\{\{(.*?)\}\}",
                      lambda match: str(expression(match.group(1), context)),
                      fields["group"]).lower()

    def cancels(self, fields, context):
        text = fields["cancel-in-progress"]
        if text.startswith("$" + "{{"):
            text = text[3:-2]
        return bool(expression(text, context))

    def assert_retained(self, fields, event):
        contexts = [self.context(event, run_id) for run_id in (101, 102, 103)]
        for context in contexts:
            self.assertFalse(self.cancels(fields, context))
        self.assertEqual(len({self.group(fields, context) for context in contexts}), 3,
                         "A third invocation must not replace a pending main run")

    def test_three_main_invocations_are_retained(self):
        for name in WORKFLOWS:
            with self.subTest(workflow=name):
                self.assert_retained(self.fields(name), "push")

    def test_benchmark_manual_invocations_are_retained_and_isolated(self):
        fields = self.fields(WORKFLOWS[0])
        self.assert_retained(fields, "workflow_dispatch")
        self.assertNotEqual(self.group(fields, self.context("push")),
                            self.group(fields, self.context("workflow_dispatch")))

    def test_candidate_coalescing_and_cancellation_are_preserved(self):
        for name in WORKFLOWS:
            fields = self.fields(name)
            for event in ("pull_request", "merge_group"):
                with self.subTest(workflow=name, event=event):
                    contexts = [self.context(event, run_id) for run_id in (101, 102, 103)]
                    self.assertEqual(len({self.group(fields, c) for c in contexts}), 1)
                    for context in contexts:
                        self.assertEqual(self.cancels(fields, context), name == WORKFLOWS[0])

    def test_distinct_workflows_events_prs_and_queue_refs_are_isolated(self):
        groups = []
        for name in WORKFLOWS:
            fields = self.fields(name)
            contexts = [self.context("push"), self.context("workflow_dispatch"),
                        self.context("pull_request", pr=7), self.context("pull_request", pr=8),
                        self.context("merge_group"),
                        self.context("merge_group", ref="refs/heads/gh-readonly-queue/main/pr-8-next")]
            groups.extend(self.group(fields, context) for context in contexts)
        self.assertEqual(len(groups), len(set(groups)))

    def test_legacy_active_cancellation_is_rejected(self):
        fields = {"group": "legacy-${{ github.ref }}", "cancel-in-progress": "true"}
        with self.assertRaises(AssertionError):
            self.assert_retained(fields, "push")

    def test_false_alone_still_loses_pending_invocations(self):
        for key in ("github.ref", "github.sha"):
            fields = {"group": "legacy-${{ " + key + " }}", "cancel-in-progress": "false"}
            with self.subTest(key=key), self.assertRaises(AssertionError):
                self.assert_retained(fields, "push")

    def test_regression_runs_in_benchmark_policy(self):
        text = (ROOT / ".github/workflows/bench-service-policy.yml").read_text(encoding="utf-8")
        self.assertIn("run: python3 -B tools/bench_direct/workflow_concurrency_test.py -v", text)


if __name__ == "__main__":
    unittest.main()
