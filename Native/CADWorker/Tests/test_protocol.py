#!/usr/bin/env python3
"""Compile and run the worker's protocol checks; they need no Open CASCADE (decision 027, Q-171 and the unit word).

    python3 -I Native/CADWorker/Tests/test_protocol.py

Builds Tests/ProtocolTests.cpp together with CADInput.cpp, CADProtocol.cpp and CADUnit.cpp using the system C++17
compiler (`CXX`, else c++, clang++ or g++) into a temporary directory, with warnings as errors and, where the compiler
has them, the address and undefined-behaviour sanitizers, then runs every case the binary lists. The cases check the
exit codes, the protocol's unit word and 72-byte header, the preflight budgets and the IGES and STEP unit scanners.
The suite skips, and says so, when no C++ compiler is installed. The full worker, which links OCCT, is exercised by
Tests/test_worker.py through script/build_cad_worker.py --test.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

WORKER = Path(__file__).resolve().parents[1]
FIXTURES = WORKER / "Tests" / "Fixtures"
SOURCES = [WORKER / "Tests" / "ProtocolTests.cpp", WORKER / "CADInput.cpp", WORKER / "CADProtocol.cpp", WORKER / "CADUnit.cpp"]
BASE_FLAGS = ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(WORKER)]
ROOT = WORKER.parents[1]
HEADER = WORKER / "CADWorker.hpp"
DECODER = ROOT / "Packages/PreviewCore/Sources/PreviewCADFormats/CADMeshDecoder.swift"
PROCESS = ROOT / "Packages/PreviewCore/Sources/PreviewCADFormats/CADWorkerProcess.swift"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def find_compiler():
    candidates = [os.environ.get("CXX", "")] + ["c++", "clang++", "g++"]
    for name in candidates:
        if name and shutil.which(name):
            return shutil.which(name)
    return None


def build(compiler, output, flags):
    command = [compiler, *flags, *map(str, SOURCES), "-o", str(output)]
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_compiler()
        if cls.compiler is None:
            raise unittest.SkipTest("no C++ compiler (set CXX, or install c++, clang++ or g++): the protocol checks did not run")
        cls.scratch = tempfile.TemporaryDirectory(prefix="cad-protocol-")
        cls.binary = Path(cls.scratch.name) / "ProtocolTests"
        cls.mode = "sanitized"
        result = build(cls.compiler, cls.binary, BASE_FLAGS + SANITIZERS)
        if result.returncode != 0:
            # No sanitizer runtime in this toolchain, or a real error: the plain build tells which. Its errors are the ones shown.
            cls.mode = "plain"
            result = build(cls.compiler, cls.binary, BASE_FLAGS)
        if result.returncode != 0:
            raise AssertionError("the protocol checks do not compile:\n" + result.stdout)
        listing = subprocess.run([str(cls.binary), "list"], stdout=subprocess.PIPE, text=True, timeout=60, check=True)
        cls.cases = listing.stdout.split()

    @classmethod
    def tearDownClass(cls):
        scratch = getattr(cls, "scratch", None)
        if scratch is not None:
            scratch.cleanup()

    def test_the_binary_lists_cases(self):
        self.assertGreaterEqual(len(self.cases), 20)
        self.assertEqual(len(self.cases), len(set(self.cases)))

    def test_every_case_passes(self):
        for name in self.cases:
            with self.subTest(case=name, mode=self.mode):
                result = subprocess.run([str(self.binary), "run", name, str(FIXTURES)], stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, timeout=300)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("0 failed", result.stdout)

    def test_an_unknown_case_is_an_error(self):
        result = subprocess.run([str(self.binary), "run", "noSuchCase", str(FIXTURES)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, timeout=60)
        self.assertEqual(result.returncode, 2)


def cpp_enum(name):
    """The enumerators of `enum class <name>` in CADWorker.hpp as {name: value}."""
    match = re.search(r"enum class " + name + r"\b[^{]*\{([^}]*)\}", HEADER.read_text(encoding="utf-8"))
    assert match, f"CADWorker.hpp has no enum class {name}"
    values = {}
    for entry in match.group(1).split(","):
        enumerator, _, number = entry.partition("=")
        values[enumerator.strip()] = int(number)
    return values


class SwiftContractTests(unittest.TestCase):
    """The Swift tables that read the worker's output must agree with the worker's enums; no Mac needed to see drift."""

    def test_the_decoders_unit_table_is_the_workers_length_unit_enum(self):
        units = cpp_enum("LengthUnit")
        self.assertEqual(units["none"], 0)
        source = DECODER.read_text(encoding="utf-8")
        declared = {int(code): name for code, name in re.findall(r"case\s+(\d+):\s*\.declared\(\.(\w+)\)", source)}
        self.assertEqual(declared, {code: name for name, code in units.items() if code != 0})
        self.assertRegex(source, r"case\s+0:\s*\.assumedMillimetres")

    def test_the_header_word_is_byte_zero_one_and_byte_one_the_unit(self):
        source = DECODER.read_text(encoding="utf-8")
        self.assertIn("units & 0xFF == 1", source)
        self.assertIn("units >> 16 == 0", source)
        self.assertIn("(units >> 8) & 0xFF", source)
        self.assertRegex(HEADER.read_text(encoding="utf-8"), r"1u \| \(static_cast<std::uint32_t>\(declared\) << 8\)")

    def test_the_transports_exit_table_is_the_workers_exit_enum(self):
        exits = cpp_enum("Exit")
        shown = {"malformed": "malformedDocument", "unsupported": "unsupportedFormat", "budget": "outputTooLarge",
                 "geometry": "geometryTooComplex"}
        source = PROCESS.read_text(encoding="utf-8")
        mapped = {int(code): failure for code, failure in re.findall(r"case\s+(\d+):\s*throw PreviewFailure\.(\w+)", source)}
        self.assertEqual(mapped, {exits[name]: failure for name, failure in shown.items()})
        self.assertRegex(source, r"case\s+0:\s*return result")
        # `internal` is the default branch: no explicit case, never a number another failure uses.
        self.assertNotIn(exits["internal"], mapped)


if __name__ == "__main__":
    unittest.main()
