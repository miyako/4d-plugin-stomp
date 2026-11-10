"""Actual-source, unchanged-SDK ownership regressions; no 4D host or network."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "64d99735d7538687118ea02ab1be76999abfaa3f"
SOURCES = (
    "4D Plugin API/4DPluginAPI.c",
    "support/4DPlugin-JSON.cpp",
    "4D Plugin API/Classes/C_TEXT.cpp",
    "4D Plugin API/Classes/ARRAY_TEXT.cpp",
    "4DPlugin-STOMP.cpp",
)


@unittest.skipUnless(sys.platform == "darwin", "native macOS engine-boundary shim")
class OwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="stomp-ownership-")
        cls.addClassCleanup(cls.scratch.cleanup)
        scratch = Path(cls.scratch.name)
        baseline = scratch / "baseline"
        paths = subprocess.check_output(
            ["git", "ls-tree", "-r", "--name-only", BASELINE,
             "4D Plugin API", "support", "4DPlugin-STOMP.cpp", "4DPlugin-STOMP.h"],
            cwd=ROOT, text=True
        ).splitlines()
        for relative in paths:
            target = baseline / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            raw = subprocess.check_output(["git", "show", f"{BASELINE}:{relative}"], cwd=ROOT)
            target.write_bytes(raw)
            if relative.startswith("4D Plugin API/"):
                if (ROOT / relative).read_bytes() != raw:
                    raise AssertionError(f"bundled SDK/wrapper changed: {relative}")
        cls.current = cls.build(ROOT, scratch / "current")
        cls.baseline = cls.build(baseline, scratch / "original")

    @classmethod
    def build(cls, source, output):
        output.mkdir()
        flags = ["-arch", "x86_64", "-g", "-O1", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined",
                 "-Wno-deprecated-declarations", "-Wno-pragma-pack",
                 "-I" + str(source),
                 "-I" + str(ROOT / "libstomp"),
                 "-I" + str(ROOT / "include/MacOS/apr-1"),
                 "-I" + str(ROOT / "include"),
                 "-I" + str(source / "4D Plugin API"),
                 "-I" + str(source / "4D Plugin API/Classes"),
                 "-I" + str(source / "support")]
        sdk = output / "sdk.o"
        result = subprocess.run(
            ["xcrun", "clang++", "-std=c++11", "-x", "c++", *flags,
             "-c", str(source / SOURCES[0]), "-o", str(sdk)],
            capture_output=True, text=True
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        executable = output / "ownership"
        result = subprocess.run(
            ["xcrun", "clang++", "-std=c++11", *flags, "-x", "objective-c++",
             str(ROOT / "tests/ownership.cpp"),
             *(str(source / relative) for relative in SOURCES[1:]),
             "-x", "none", str(sdk), "-framework", "Cocoa",
             "-framework", "CoreFoundation", "-framework", "CoreGraphics",
             str(ROOT / "a/libapr-1.a"), str(ROOT / "a/libjsoncpp.a"),
             "-o", str(executable)], capture_output=True, text=True
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return executable

    def run_scenario(self, executable, scenario, empty):
        return subprocess.run(
            [str(executable), scenario, empty],
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                 "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"},
            capture_output=True, text=True, timeout=30
        )

    def test_fixed_sources(self):
        for scenario in ("setters", "getters", "rejected", "arrays", "scalars",
                         "object-write", "array-write"):
            for empty in ("allocated", "null"):
                with self.subTest(scenario=scenario, empty=empty):
                    result = self.run_scenario(self.current, scenario, empty)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    if scenario == "rejected":
                        self.assertEqual(result.stderr.count("PA_GetObjectProperty failed:"), 4)

    def test_immutable_baseline_exposes_both_leaks(self):
        for scenario, empty in (("setters", "allocated"), ("getters", "allocated"),
                                ("getters", "null")):
            with self.subTest(scenario=scenario, empty=empty):
                result = self.run_scenario(self.baseline, scenario, empty)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn(f"{scenario}: allocation imbalance:", result.stderr)
                print(f"baseline {BASELINE}: {result.stderr.strip()}")

    def test_baseline_write_buffers_are_freed_before_consumption(self):
        for scenario in ("object-write", "array-write"):
            with self.subTest(scenario=scenario):
                result = self.run_scenario(self.baseline, scenario, "null")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("ERROR: AddressSanitizer: heap-use-after-free", result.stderr)
                print(f"baseline {BASELINE}: {scenario}: ASan heap-use-after-free")

    def test_baseline_null_empty_setters_do_not_manufacture_a_leak(self):
        result = self.run_scenario(self.baseline, "setters", "null")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unchanged_array_and_scalar_paths(self):
        for scenario in ("arrays", "scalars"):
            for empty in ("allocated", "null"):
                with self.subTest(scenario=scenario, empty=empty):
                    result = self.run_scenario(self.baseline, scenario, empty)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
