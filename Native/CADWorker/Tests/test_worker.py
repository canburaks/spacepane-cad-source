#!/usr/bin/env python3
"""Independent process checks; synthetic fixtures never use the private model."""
import argparse
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import sys
import time

options = argparse.ArgumentParser()
options.add_argument("--worker", type=Path, required=True)
options.add_argument("--generator", type=Path)
arguments, remaining = options.parse_known_args()


class WorkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="prev-cad-tests-")
        cls.fixtures = Path(cls.temporary.name)
        if arguments.generator:
            subprocess.run([str(arguments.generator), str(cls.fixtures)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
        else:
            cls.fixtures = Path(__file__).parent / "Fixtures"

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_worker(self, source, kind="step", expected=0):
        response = subprocess.run([str(arguments.worker), kind], input=source,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        self.assertEqual(response.returncode, expected)
        self.assertEqual(response.stderr, b"")
        if expected:
            self.assertEqual(response.stdout, b"")
        return response.stdout

    def mesh(self, name, kind):
        data = self.run_worker((self.fixtures / (name + "." + kind)).read_bytes(), kind)
        self.assertEqual(data[:8], b"PRVCAD01")
        vertices, indices, faces, unit, *bounds = struct.unpack_from("<4I6d", data, 8)
        # The fourth word is 1 | (declared unit << 8): positions are millimetres, the second byte names the unit the
        # file declared (0 none, 1 um, 2 mm, 3 cm, 4 m, 5 in, 6 ft) and the upper half is reserved.
        self.assertEqual(unit & 0xFF, 1)
        self.assertEqual(unit >> 16, 0)
        self.declared_unit = (unit >> 8) & 0xFF
        self.assertEqual(len(data), 72 + vertices * 24 + indices * 4)
        self.assertLessEqual(len(data), 32 * 1024 * 1024)
        points = [struct.unpack_from("<6f", data, 72 + index * 24) for index in range(vertices)]
        topology = struct.unpack_from("<" + str(indices) + "I", data, 72 + vertices * 24)
        self.assertEqual(indices % 3, 0)
        self.assertTrue(all(index < vertices for index in topology))
        self.assertTrue(all(math.isfinite(value) for point in points for value in point))
        for point in points:
            self.assertLessEqual(max(map(abs, point[:3])), 1.00001)
            self.assertAlmostEqual(sum(value * value for value in point[3:]), 1.0, places=5)
        return faces, bounds, points, topology

    def test_empty_and_malformed_are_typed(self):
        self.run_worker(b"", expected=2)
        self.run_worker(b"arbitrary synthetic malformed CAD", expected=2)
        self.run_worker(b"arbitrary synthetic malformed CAD", kind="iges", expected=2)

    def test_external_references_decline(self):
        self.run_worker(b"ISO-10303-21;HEADER;ENDSEC;DATA;#1=EXTERNALLY_DEFINED_ITEM('secret',#2);ENDSEC;END-ISO-10303-21;", expected=3)

    def test_input_allocation_is_bounded(self):
        self.run_worker(b"x" * (16 * 1024 * 1024 + 1), expected=4)

    def test_argument_is_closed(self):
        self.run_worker(b"", kind="other", expected=3)

    def test_box_bounds_units_indices_and_normals(self):
        for kind in ("step", "iges"):
            with self.subTest(kind=kind):
                faces, bounds, points, topology = self.mesh("box", kind)
                self.assertEqual(faces, 6)
                self.assertEqual(bounds, [0., 0., 0., 10., 20., 30.])
                self.assertEqual(len(topology), 36)
                # Outward triangle winding must enclose the independent box volume.
                volume = 0
                for offset in range(0, len(topology), 3):
                    a, b, c = [points[topology[offset + corner]][:3] for corner in range(3)]
                    volume += a[0] * (b[1]*c[2]-b[2]*c[1]) + a[1] * (b[2]*c[0]-b[0]*c[2]) + a[2] * (b[0]*c[1]-b[1]*c[0])
                self.assertAlmostEqual(volume / 6 * 15**3, 6000, places=2)

    def test_the_declared_unit_is_reported_in_the_header_word(self):
        # box.step and box.iges are checked in; the generator adds the others. Both formats declare millimetres except
        # the inch box, which both declare in inches (OCCT converts the geometry either way, see the test below).
        expected = {"box": 2, "translated": 2, "cylinder": 2, "cone": 2, "sphere": 2, "trimmed": 2, "inch": 5}
        for kind in ("step", "iges"):
            for name, code in expected.items():
                if name != "box" and not arguments.generator:
                    continue
                with self.subTest(kind=kind, name=name):
                    self.mesh(name, kind)
                    self.assertEqual(self.declared_unit, code)

    def test_a_file_that_declares_no_readable_unit_reports_none(self):
        if not arguments.generator:
            self.skipTest("analytical fixture generator was not supplied")
        # The STEP box with its unit context renamed has no GLOBAL_UNIT_ASSIGNED_CONTEXT the scanner can read. The
        # importer may still decline the file; only when it succeeds does the word have to say "none".
        original = (self.fixtures / "box.step").read_bytes()
        stripped = original.replace(b"GLOBAL_UNIT_ASSIGNED_CONTEXT", b"GLOBAL_UNIT_ASSIGNED_CONTEXX")
        response = subprocess.run([str(arguments.worker), "step"], input=stripped, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=20)
        self.assertEqual(response.stderr, b"")
        if response.returncode == 0:
            self.assertEqual((struct.unpack_from("<I", response.stdout, 20)[0] >> 8) & 0xFF, 0)

    def test_transform_and_inch_units_are_applied(self):
        if not arguments.generator:
            self.skipTest("analytical fixture generator was not supplied")
        for kind in ("step", "iges"):
            for name, expected in (("translated", [100., 200., 300., 110., 220., 330.]),
                                   ("inch", [0., 0., 0., 25.4, 50.8, 76.2])):
                with self.subTest(kind=kind, name=name):
                    _, bounds, _, _ = self.mesh(name, kind)
                    for actual, target in zip(bounds, expected):
                        self.assertAlmostEqual(actual, target, places=8)

    def test_surface_trim_preserves_square_hole(self):
        if not arguments.generator:
            self.skipTest("analytical fixture generator was not supplied")
        for kind in ("step", "iges"):
            with self.subTest(kind=kind):
                faces, bounds, points, topology = self.mesh("trimmed", kind)
                self.assertEqual(faces, 1)
                self.assertEqual(bounds, [0., 0., 0., 10., 10., 0.])
                area = 0
                for offset in range(0, len(topology), 3):
                    a, b, c = [points[topology[offset + corner]][:3] for corner in range(3)]
                    centroid = [(a[axis]+b[axis]+c[axis])/3*5+5 for axis in (0, 1)]
                    self.assertFalse(3 < centroid[0] < 7 and 3 < centroid[1] < 7)
                    area += abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2*25
                self.assertAlmostEqual(area, 84., places=4)

    def test_curved_surfaces_and_collapsed_poles(self):
        if not arguments.generator:
            self.skipTest("analytical fixture generator was not supplied")
        for kind in ("step", "iges"):
            for name, expected_faces in (("cylinder", 3), ("cone", 2), ("sphere", 1)):
                with self.subTest(kind=kind, name=name):
                    faces, _, _, topology = self.mesh(name, kind)
                    self.assertEqual(faces, expected_faces)
                    self.assertGreater(len(topology), 36)

    def test_entity_and_depth_budgets_precede_kernel(self):
        header = b"ISO-10303-21;HEADER;ENDSEC;DATA;"
        footer = b"ENDSEC;END-ISO-10303-21;"
        self.run_worker(header + b"#1=(" * 129 + b")" * 129 + footer, expected=4)
        entities = b"".join(("#%d=CARTESIAN_POINT('',(0.,0.,0.));" % index).encode()
                            for index in range(1, 100002))
        self.run_worker(header + entities + footer, expected=4)

    def test_source_hash_metadata_and_source_directory(self):
        for kind in ("step", "iges"):
            with self.subTest(kind=kind):
                original = (self.fixtures / ("box." + kind)).read_bytes()
                safe_mesh = self.run_worker(original, kind)
                if kind == "step":
                    decorated = original.replace(b"'synthetic'", b"'password=synthetic-secret'")
                else:
                    lines = original.splitlines()
                    lines[0] = b"password=synthetic-secret".ljust(72) + lines[0][72:]
                    decorated = b"\n".join(lines) + b"\n"
                directory = Path(self.temporary.name) / ("mutation-" + kind)
                directory.mkdir()
                source = directory / ("private." + kind)
                source.write_bytes(decorated)
                before = hashlib.sha256(source.read_bytes()).digest()
                result = self.run_worker(source.read_bytes(), kind)
                self.assertEqual(result, safe_mesh)
                self.assertNotIn(b"synthetic-secret", result)
                self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)
                self.assertEqual(list(directory.iterdir()), [source])

    def test_owned_process_can_be_killed_and_reaped(self):
        process = subprocess.Popen([str(arguments.worker), "step"], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(.05)  # child is waiting for its bounded source stream
            process.kill()
            output, diagnostics = process.communicate(timeout=2)
            self.assertLess(process.returncode, 0)
            self.assertEqual(output, b"")
            self.assertEqual(diagnostics, b"")
        finally:
            if process.poll() is None:
                process.kill(); process.wait(timeout=2)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + remaining)
