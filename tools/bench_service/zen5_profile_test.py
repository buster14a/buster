#!/usr/bin/env python3
"""Pin the held zen5-calibration-v1 recipe profile (#426) to its sources.

The installed profile bytes, the compiled copy in zen5_calibration_profile.h,
the tool digests it pins, the registry's held state and the dispatch
workflow's reviewed budget must agree. The native self-test
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


def profile_fields() -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in PROFILE.read_text(encoding="utf-8").splitlines():
        key, _, value = line.partition("=")
        if key in fields:
            raise AssertionError(f"duplicate profile key {key}")
        fields[key] = value
    return fields


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
        cap = int(re.search(r"#define BQ_RECIPE_PROFILE_CAP ([0-9]+)u", QUEUE_HEADER.read_text(encoding="utf-8")).group(1))
        self.assertLessEqual(len(PROFILE.read_bytes()), cap)

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
                    "source-manifest": "BQ-SOURCE-V1", "status": "held-until-broker-stages",
                    "source": "base-equals-candidate", "trusted-builds": "5", "pairs": "360",
                    "timed-children": "720", "ab-authorized": "false"}
        for key, value in expected.items():
            self.assertEqual(fields.get(key), value, key)
        self.assertTrue((ROOT / fields["workload"]).is_file())
        self.assertLess(int(fields["timing-reserve-seconds"]), int(fields["budget-seconds"]))
        self.assertGreater(int(fields["interblock-gap-ns"]), 0)

    def test_registry_holds_the_recipe(self):
        queue = QUEUE.read_text(encoding="utf-8")
        self.assertIn("BQ_RECIPE_ZEN5_CALIBRATION", function_body(queue, "bq_recipe_blocked"))
        for name in ("bq_recipe_admitted", "bq_recipe_service"):
            with self.subTest(function=name):
                self.assertNotIn("ZEN5", function_body(queue, name))
        self.assertIn('"bench_service_zen5_recipe"', queue)

    def test_dispatch_budget_is_the_enforced_profile_budget(self):
        dispatch = DISPATCH.read_text(encoding="utf-8")
        arm = re.findall(r"(?m)^ +zen5-calibration-v1\) runtime_budget=([0-9]+) ;;$", dispatch)
        self.assertEqual(arm, [profile_fields()["budget-seconds"]])
        recipe = RECIPE.read_text(encoding="utf-8")
        self.assertIn('bench_service_zen5_profile_u64("budget-seconds", &recipe->budget_seconds)', recipe)
        self.assertIn("recipe->deadline_ns = recipe->started_ns + recipe->budget_seconds * 1000000000ull;", recipe)


if __name__ == "__main__":
    result = unittest.main(verbosity=1, exit=False).result
    sys.exit(0 if result.wasSuccessful() else 1)
