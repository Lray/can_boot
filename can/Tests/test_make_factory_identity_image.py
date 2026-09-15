#!/usr/bin/env python3
import importlib.util
import pathlib
import struct
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "make_factory_identity_image.py"
SPEC = importlib.util.spec_from_file_location("make_factory_identity_image", SCRIPT)
identity_image = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(identity_image)


class FactoryIdentityImageTest(unittest.TestCase):
    def test_image_contains_exactly_four_little_endian_u32_values(self):
        identity = identity_image.make_identity(1, 2, 3, 4)

        self.assertEqual(len(identity), 16)
        self.assertEqual(identity, struct.pack("<IIII", 1, 2, 3, 4))

    def test_intel_hex_contains_only_the_identity_payload(self):
        identity = identity_image.make_identity(1, 2, 3, 4)
        lines = identity_image.make_intel_hex(identity).splitlines()

        self.assertEqual(lines[0], ":02000004081FD3")
        self.assertTrue(lines[1].startswith(":10"))
        self.assertEqual(lines[1][3:7], "4000")
        self.assertEqual(lines[-1], ":00000001FF")
        self.assertEqual(len(lines), 3)

    def test_rejects_non_four_word_payload_for_hex(self):
        with self.assertRaises(ValueError):
            identity_image.make_intel_hex(b"\x00" * 15)


if __name__ == "__main__":
    unittest.main()
