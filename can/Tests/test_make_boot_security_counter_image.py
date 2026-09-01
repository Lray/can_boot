#!/usr/bin/env python3
import importlib.util
import pathlib
import struct
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "make_boot_security_counter_image.py"
SPEC = importlib.util.spec_from_file_location("make_boot_security_counter_image", SCRIPT)
counter_image = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(counter_image)


class SecurityCounterImageTest(unittest.TestCase):
    def test_initial_page_is_valid_and_erased_after_record(self):
        page = counter_image.make_page(1)

        self.assertEqual(len(page), 0x2000)
        counter_image.validate_page(page, 1, 0)
        self.assertEqual(page[counter_image.RECORD_SIZE:], b"\xFF" * (0x2000 - counter_image.RECORD_SIZE))

    def test_record_matches_boot_layout_and_checksum(self):
        page = counter_image.make_page(0x12345678, sequence=9)
        fields = struct.unpack_from("<IHHIIII", page, 0)

        self.assertEqual(fields[0], 0x53454356)
        self.assertEqual(fields[1], 1)
        self.assertEqual(fields[2], 64)
        self.assertEqual(fields[3], 9)
        self.assertEqual(fields[4], 0x12345678)
        self.assertEqual(fields[6], counter_image.record_checksum(9, 0x12345678, fields[5]))

    def test_intel_hex_targets_security_state_zero(self):
        lines = counter_image.make_intel_hex(counter_image.make_page(1), 0x081F6000).splitlines()

        self.assertEqual(lines[0], ":02000004081FD3")
        self.assertTrue(lines[1].startswith(":10600000"))
        self.assertEqual(lines[-1], ":00000001FF")


if __name__ == "__main__":
    unittest.main()
