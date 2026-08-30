#!/usr/bin/env python3
import importlib.util
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "make_ecu_mcuboot_bundle.py"
SPEC = importlib.util.spec_from_file_location("make_ecu_mcuboot_bundle", SCRIPT)
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class PayloadAutomationTest(unittest.TestCase):
    @staticmethod
    def make_slot_zero_payload(stack_pointer: int) -> bytes:
        reset_handler = bundle.SLOT_PAYLOAD_BASE[0] | 1
        return stack_pointer.to_bytes(4, "little") + reset_handler.to_bytes(4, "little")

    def test_accepts_aligned_stack_pointer_inside_final_sram_bank(self):
        payload = self.make_slot_zero_payload(0x2026FFF8)

        bundle.validate_payload(payload, 0)

    def test_rejects_stack_pointer_at_exclusive_sram_upper_bound(self):
        payload = self.make_slot_zero_payload(0x20270000)

        with self.assertRaisesRegex(ValueError, "outside Boot-valid STM32U5 SRAM"):
            bundle.validate_payload(payload, 0)

    def test_slot_padding_keeps_body_and_trailer_erased(self):
        compact = bytes([0xA5]) * 32

        padded = bundle.pad_image_to_slot(compact)

        self.assertEqual(len(padded), bundle.SLOT_SIZE)
        self.assertEqual(padded[:len(compact)], compact)
        self.assertEqual(padded[len(compact):bundle.TRAILER_OFFSET], b"\xFF" * (bundle.TRAILER_OFFSET - len(compact)))
        self.assertEqual(padded[bundle.TRAILER_OFFSET:], b"\xFF" * (bundle.SLOT_SIZE - bundle.TRAILER_OFFSET))

    def test_pending_trailer_sets_only_boot_magic(self):
        image = bytearray(b"\x00" * bundle.TRAILER_OFFSET)
        image.extend(b"\xFF" * (bundle.SLOT_SIZE - bundle.TRAILER_OFFSET))

        pending = bundle.apply_pending_trailer(bytes(image))

        self.assertEqual(len(pending), bundle.SLOT_SIZE)
        self.assertEqual(
            pending[bundle.TRAILER_OFFSET:bundle.MAGIC_OFFSET],
            b"\xFF" * (bundle.MAGIC_OFFSET - bundle.TRAILER_OFFSET),
        )
        self.assertEqual(pending[bundle.MAGIC_OFFSET:], bundle.BOOT_MAGIC)

    def test_confirmed_trailer_sets_boot_magic_copy_done_and_image_ok(self):
        image = bytearray(b"\x00" * bundle.TRAILER_OFFSET)
        image.extend(b"\xFF" * (bundle.SLOT_SIZE - bundle.TRAILER_OFFSET))

        confirmed = bundle.apply_confirmed_trailer(bytes(image))

        self.assertEqual(len(confirmed), bundle.SLOT_SIZE)
        self.assertEqual(confirmed[bundle.TRAILER_OFFSET:bundle.COPY_DONE_OFFSET], b"\xFF" * 16)
        self.assertEqual(confirmed[bundle.COPY_DONE_OFFSET], 0x01)
        self.assertEqual(confirmed[bundle.COPY_DONE_OFFSET + 1:bundle.IMAGE_OK_OFFSET], b"\xFF" * 15)
        self.assertEqual(confirmed[bundle.IMAGE_OK_OFFSET], 0x01)
        self.assertEqual(confirmed[bundle.IMAGE_OK_OFFSET + 1:bundle.MAGIC_OFFSET], b"\xFF" * 15)
        self.assertEqual(confirmed[bundle.MAGIC_OFFSET:], bundle.BOOT_MAGIC)

    def test_pending_and_confirmed_trailers_are_distinct_contracts(self):
        pending = bundle.build_trailer(bundle.TRAILER_PENDING)
        confirmed = bundle.build_trailer(bundle.TRAILER_CONFIRMED)

        self.assertEqual(pending[0], 0xFF)
        self.assertEqual(pending[bundle.COPY_DONE_OFFSET - bundle.TRAILER_OFFSET], 0xFF)
        self.assertEqual(pending[bundle.IMAGE_OK_OFFSET - bundle.TRAILER_OFFSET], 0xFF)
        self.assertEqual(confirmed[bundle.COPY_DONE_OFFSET - bundle.TRAILER_OFFSET], 0x01)
        self.assertEqual(confirmed[bundle.IMAGE_OK_OFFSET - bundle.TRAILER_OFFSET], 0x01)
        self.assertEqual(pending[-len(bundle.BOOT_MAGIC):], bundle.BOOT_MAGIC)
        self.assertEqual(confirmed[-len(bundle.BOOT_MAGIC):], bundle.BOOT_MAGIC)

    def test_pending_trailer_validator_rejects_erased_flags_and_bad_magic(self):
        image = bundle.apply_pending_trailer(
            bundle.pad_image_to_slot(bytes([0xA5]) * 32)
        )
        bundle.validate_slot_trailer(image, bundle.TRAILER_PENDING)

        invalid_offsets = (
            bundle.TRAILER_OFFSET,
            bundle.COPY_DONE_OFFSET,
            bundle.IMAGE_OK_OFFSET,
            bundle.MAGIC_OFFSET,
        )
        for offset in invalid_offsets:
            with self.subTest(offset=offset):
                invalid = bytearray(image)
                invalid[offset] ^= 0x01
                with self.assertRaisesRegex(ValueError, "required pending state"):
                    bundle.validate_slot_trailer(bytes(invalid), bundle.TRAILER_PENDING)

        erased = bundle.pad_image_to_slot(bytes([0xA5]) * 32)
        with self.assertRaisesRegex(ValueError, "required pending state"):
            bundle.validate_slot_trailer(erased, bundle.TRAILER_PENDING)

    def test_defaults_to_cmake_slot_zero_payload(self):
        self.assertEqual(
            bundle.default_payload_path(),
            bundle.REPO_ROOT / "build" / "Debug-slot0" / "Can.bin",
        )


if __name__ == "__main__":
    unittest.main()
