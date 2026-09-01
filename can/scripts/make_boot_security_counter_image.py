#!/usr/bin/env python3
"""Create an initial STM32U5 Boot security-counter flash page.

The Boot port owns two redundant 8 KiB security-state pages.  This tool emits
one valid initial record at the selected page base and leaves the remainder
erased.  It produces both a raw page image and an Intel HEX file with the
absolute flash address, so the delivery is usable with a programmer without
reconstructing the Boot-private record format by hand.
"""

import argparse
import hashlib
import pathlib
import struct


FLASH_PAGE_SIZE = 0x2000
FLASH_STATE_BASES = {
    0: 0x081F6000,
    1: 0x081F8000,
}

RECORD_MAGIC = 0x53454356
RECORD_FORMAT = 1
RECORD_SIZE = 64
INITIAL_SEQUENCE = 0
INITIAL_FLAGS = 0
CHECKSUM_SALT = 0x5A5AA5A5


def parse_u32(value: str) -> int:
    parsed = int(value, 0)
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"{value} is outside uint32 range")
    return parsed


def record_checksum(sequence: int, security_counter: int, flags: int) -> int:
    return (RECORD_MAGIC ^ RECORD_FORMAT ^ RECORD_SIZE ^ sequence ^
            security_counter ^ flags ^ CHECKSUM_SALT) & 0xFFFFFFFF


def make_record(security_counter: int, sequence: int = INITIAL_SEQUENCE,
                flags: int = INITIAL_FLAGS) -> bytes:
    if not all(0 <= value <= 0xFFFFFFFF
               for value in (security_counter, sequence, flags)):
        raise ValueError("security counter record field is outside uint32 range")
    record = struct.pack(
        "<IHHIIII",
        RECORD_MAGIC,
        RECORD_FORMAT,
        RECORD_SIZE,
        sequence,
        security_counter,
        flags,
        record_checksum(sequence, security_counter, flags),
    )
    record += b"\xFF" * (RECORD_SIZE - len(record))
    if len(record) != RECORD_SIZE:
        raise ValueError("security counter record size mismatch")
    return record


def validate_page(page: bytes, security_counter: int, sequence: int) -> None:
    if len(page) != FLASH_PAGE_SIZE:
        raise ValueError("security state page size mismatch")
    magic, record_format, record_size, actual_sequence, actual_counter, flags, checksum = \
        struct.unpack_from("<IHHIIII", page, 0)
    if (magic, record_format, record_size, actual_sequence, actual_counter) != (
            RECORD_MAGIC, RECORD_FORMAT, RECORD_SIZE, sequence, security_counter):
        raise ValueError("security state record fields do not match requested values")
    if checksum != record_checksum(actual_sequence, actual_counter, flags):
        raise ValueError("security state record checksum is invalid")
    if page[RECORD_SIZE:] != b"\xFF" * (FLASH_PAGE_SIZE - RECORD_SIZE):
        raise ValueError("security state page tail must remain erased")


def make_page(security_counter: int, sequence: int = INITIAL_SEQUENCE) -> bytes:
    page = make_record(security_counter, sequence)
    page += b"\xFF" * (FLASH_PAGE_SIZE - len(page))
    validate_page(page, security_counter, sequence)
    return page


def intel_hex_record(address: int, record_type: int, data: bytes) -> str:
    if not 0 <= address <= 0xFFFF:
        raise ValueError("Intel HEX record address is outside uint16 range")
    if not 0 <= record_type <= 0xFF or len(data) > 0xFF:
        raise ValueError("invalid Intel HEX record")
    values = bytes([len(data), address >> 8, address & 0xFF, record_type]) + data
    checksum = (-sum(values)) & 0xFF
    return ":" + (values + bytes([checksum])).hex().upper()


def make_intel_hex(page: bytes, base_address: int) -> str:
    upper = base_address >> 16
    lines = [intel_hex_record(0, 4, struct.pack(">H", upper))]
    for offset in range(0, len(page), 16):
        lines.append(intel_hex_record((base_address + offset) & 0xFFFF, 0,
                                      page[offset:offset + 16]))
    lines.append(intel_hex_record(0, 1, b""))
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create an initial Boot security-counter state-page image."
    )
    parser.add_argument("--security-counter", required=True, type=parse_u32,
                        help="initial monotonic security counter")
    parser.add_argument("--area", type=int, choices=tuple(FLASH_STATE_BASES), default=0,
                        help="Boot security-state page: 0 (0x081F6000) or 1 (0x081F8000)")
    parser.add_argument("--sequence", type=parse_u32, default=INITIAL_SEQUENCE,
                        help="record sequence, normally zero for initial provisioning")
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()

    page = make_page(args.security_counter, args.sequence)
    base_address = FLASH_STATE_BASES[args.area]
    prefix = f"security-counter-state{args.area}"
    args.out_dir.mkdir(parents=True, exist_ok=True)
    bin_path = args.out_dir / f"{prefix}.bin"
    hex_path = args.out_dir / f"{prefix}.hex"
    info_path = args.out_dir / f"{prefix}.info"

    bin_path.write_bytes(page)
    hex_path.write_text(make_intel_hex(page, base_address), encoding="ascii")
    info_path.write_text(
        "\n".join((
            "source=stm32u5-boot-security-counter",
            f"flash_address=0x{base_address:08X}",
            f"flash_page_size=0x{FLASH_PAGE_SIZE:X}",
            f"record_size={RECORD_SIZE}",
            f"record_sequence={args.sequence}",
            f"security_counter={args.security_counter}",
            f"sha256={hashlib.sha256(page).hexdigest()}",
            "",
        )),
        encoding="ascii",
    )
    print(f"wrote {bin_path}")
    print(f"wrote {hex_path}")
    print(f"flash_address=0x{base_address:08X}")
    print(f"security_counter={args.security_counter}")
    print(f"sha256={hashlib.sha256(page).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
