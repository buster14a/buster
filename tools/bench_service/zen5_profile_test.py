#!/usr/bin/env python3
"""Pin the served zen5-calibration-v1 recipe profile (#426) to its sources.

The installed profile bytes, the compiled copy in zen5_calibration_profile.h,
the tool digests it pins, the registry's served state, the broker stage
contract (zen5_stage.h) and the dispatch workflow's wait must agree. The native self-test
(`build/build bench_service_zen5_recipe_self_test`) exercises the recipe
itself; this check needs only the repository text.
"""

from __future__ import annotations

import hashlib
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / "tools" / "bench_service"
PROFILE = SERVICE / "profiles" / "zen5-calibration-v1.recipe"
HEADER = SERVICE / "zen5_calibration_profile.h"
QUEUE = SERVICE / "queue.c"
QUEUE_HEADER = SERVICE / "queue.h"
RECIPE = SERVICE / "zen5_recipe.c"
DISPATCH = ROOT / ".github" / "workflows" / "9700x-service-dispatch.yml"
BROKER = SERVICE / "systemd_broker.c"
STAGE = SERVICE / "zen5_stage.h"


def profile_fields() -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in PROFILE.read_text(encoding="utf-8").splitlines():
        key, _, value = line.partition("=")
        if key in fields:
            raise AssertionError(f"duplicate profile key {key}")
        fields[key] = value
    return fields


def queue_macro(name: str) -> int:
    """Evaluate an unsigned integer macro of queue.h built from other such macros."""
    text = QUEUE_HEADER.read_text(encoding="utf-8").replace("\\\n", " ")
    macros = dict(re.findall(r"^#define (BQ_[A-Z0-9_]+) ([0-9]+u|\(.*\))$", text, re.MULTILINE))
    expression = macros[name]
    while (found := re.search(r"BQ_[A-Z0-9_]+", expression)) is not None:
        expression = expression.replace(found.group(0), macros[found.group(0)])
    expression = re.sub(r"([0-9]+)u", r"\1", expression)
    if not re.fullmatch(r"[0-9+*() ]+", expression):
        raise AssertionError(f"{name} is not a sum of products: {expression}")
    return eval(expression)


def function_body(source: str, name: str) -> str:
    start = source.index(f"bool {name}(BqRecipe recipe)")
    return source[start:source.index("\n}\n", start)]


class Zen5ProfileTest(unittest.TestCase):
    def test_compiled_copy_is_byte_identical(self):
        header = HEADER.read_text(encoding="utf-8")
        literal = header[header.index("#define BQ_ZEN5_CALIBRATION_PROFILE"):header.index("#endif")]
        compiled = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', literal)).replace("\\n", "\n")
        self.assertEqual(compiled.encode("utf-8"), PROFILE.read_bytes())

    def test_profile_fits_the_installed_recipe_cap(self):
        self.assertLessEqual(len(PROFILE.read_bytes()), queue_macro("BQ_RECIPE_PROFILE_CAP"))

    def test_pins_match_the_repository_tools(self):
        fields = profile_fields()
        pins = [key for key in fields if key.endswith("-sha256")]
        self.assertEqual(sorted(pins), ["pmu-capture-sha256", "pmu-common-sha256", "pmu-replay-sha256", "pmu-sha256"])
        for pin in pins:
            path = ROOT / fields[pin.removesuffix("-sha256")]
            with self.subTest(path=str(path)):
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), fields[pin])

    def test_fixed_design_and_no_authorization(self):
        fields = profile_fields()
        expected = {"schema": "1", "recipe": "zen5-calibration-v1", "repository": "buster14a/buster",
                    "source-manifest": "BQ-SOURCE-V1", "status": "served-by-broker-v2",
                    "source": "base-equals-candidate", "trusted-builds": "5", "pairs": "360",
                    "timed-children": "720", "ab-authorized": "false"}
        for key, value in expected.items():
            self.assertEqual(fields.get(key), value, key)
        self.assertTrue((ROOT / fields["workload"]).is_file())
        self.assertLess(int(fields["timing-reserve-seconds"]), int(fields["budget-seconds"]))
        self.assertGreater(int(fields["interblock-gap-ns"]), 0)

    def test_registry_serves_the_recipe(self):
        queue = QUEUE.read_text(encoding="utf-8")
        self.assertNotIn("ZEN5", function_body(queue, "bq_recipe_blocked"))
        for name in ("bq_recipe_admitted", "bq_recipe_service"):
            with self.subTest(function=name):
                self.assertIn("BQ_RECIPE_ZEN5_CALIBRATION", function_body(queue, name))
        self.assertIn('"bench_service_zen5_recipe"', queue)

    def test_stage_contract_matches_the_profile(self):
        # The broker builds the stage argv from zen5_stage.h; the recipe
        # refuses to run unless the profile agrees (bench_service_zen5_run).
        fields = profile_fields()
        stage = STAGE.read_text(encoding="utf-8")
        def define(name: str) -> str:
            return re.search(rf'#define {name} "([^"]*)"', stage).group(1)
        self.assertEqual(define("BQ_ZEN5_STAGE_WORKLOAD"), fields["workload"])
        self.assertEqual(define("BQ_ZEN5_STAGE_CPU"), fields["cpu"])
        self.assertEqual(re.search(r"#define BQ_ZEN5_STAGE_CPU_NUMBER ([0-9]+)u", stage).group(1), fields["cpu"])
        self.assertEqual(define("BQ_ZEN5_STAGE_PMU_TOOL"), fields["pmu-capture"])
        self.assertEqual(define("BQ_ZEN5_STAGE_PMU_TOOL_NAME"), Path(fields["pmu-capture"]).name)
        self.assertEqual(define("BQ_ZEN5_STAGE_RECIPE"), fields["recipe"])

    def test_dispatch_waits_for_the_unit_bound_not_the_recipe_budget(self):
        # The profile budget starts after materialization inside the outer
        # unit; the dispatch wait is the broker's RuntimeMaxSec (review S1).
        dispatch = DISPATCH.read_text(encoding="utf-8")
        arm = re.findall(r"(?m)^ +zen5-calibration-v1\) runtime_budget=([0-9]+) ;;$", dispatch)
        runtime = re.findall(r'"--property=RuntimeMaxSec=([1-9][0-9]*)us"', BROKER.read_text(encoding="utf-8"))
        self.assertEqual(len(runtime), 1)
        self.assertEqual(arm, [str(int(runtime[0]) // 1000000)])
        self.assertLess(int(profile_fields()["budget-seconds"]), int(arm[0]))
        recipe = RECIPE.read_text(encoding="utf-8")
        self.assertIn('bench_service_zen5_profile_u64("budget-seconds", &recipe->budget_seconds)', recipe)
        self.assertIn("recipe->deadline_ns = recipe->started_ns + recipe->budget_seconds * 1000000000ull;", recipe)


if __name__ == "__main__":
    result = unittest.main(verbosity=1, exit=False).result
    sys.exit(0 if result.wasSuccessful() else 1)
