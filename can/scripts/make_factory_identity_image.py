#!/usr/bin/env python3
"""Create a 16-byte STM32 factory-identity provisioning image."""

import argparse
import hashlib
import pathlib
import struct


FACTORY_IDENTITY_ADDRESS = 0x081F4000
FACTORY_IDENTITY_SIZE = 16


def parse_u32(value: str) -> int:
    parsed = int(value, 0)
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"{value} is outside uint32 range")
    return parsed


def make_identity(vendor_id: int, product_code: int,
                  revision_number: int, serial_number: int) -> bytes:
    values = (vendor_id, product_code, revision_number, serial_number)
    if not all(0 <= value <= 0xFFFFFFFF for value in values):
        raise ValueError("factory identity field is outside uint32 range")
    identity = struct.pack("<IIII", *values)
    if len(identity) != FACTORY_IDENTITY_SIZE:
        raise ValueError("factory identity size mismatch")
    return identity


def intel_hex_record(address: int, record_type: int, data: bytes) -> str:
    values = bytes([len(data), address >> 8, address & 0xFF, record_type]) + data
    return ":" + (values + bytes([(-sum(values)) & 0xFF])).hex().upper()


def make_intel_hex(identity: bytes) -> str:
    if len(identity) != FACTORY_IDENTITY_SIZE:
        raise ValueError("factory identity must contain exactly 16 bytes")
    return "\n".join((
        intel_hex_record(0, 4, struct.pack(">H", FACTORY_IDENTITY_ADDRESS >> 16)),
        intel_hex_record(FACTORY_IDENTITY_ADDRESS & 0xFFFF, 0, identity),
        intel_hex_record(0, 1, b""),
        "",
    ))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create the four-word factory identity provisioning image."
    )
    parser.add_argument("--vendor-id", required=True, type=parse_u32)
    parser.add_argument("--product-code", required=True, type=parse_u32)
    parser.add_argument("--revision-number", required=True, type=parse_u32)
    parser.add_argument("--serial-number", required=True, type=parse_u32)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()

    identity = make_identity(args.vendor_id, args.product_code,
                             args.revision_number, args.serial_number)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    bin_path = args.out_dir / "factory-identity.bin"
    hex_path = args.out_dir / "factory-identity.hex"
    info_path = args.out_dir / "factory-identity.info"
    bin_path.write_bytes(identity)
    hex_path.write_text(make_intel_hex(identity), encoding="ascii")
    info_path.write_text(
        "\n".join((
            f"flash_address=0x{FACTORY_IDENTITY_ADDRESS:08X}",
            f"size={FACTORY_IDENTITY_SIZE}",
            f"vendor_id={args.vendor_id}",
            f"product_code={args.product_code}",
            f"revision_number={args.revision_number}",
            f"serial_number={args.serial_number}",
            f"sha256={hashlib.sha256(identity).hexdigest()}",
            "",
        )),
        encoding="ascii",
    )
    print(f"wrote {bin_path}")
    print(f"wrote {hex_path}")
    print(f"flash_address=0x{FACTORY_IDENTITY_ADDRESS:08X}")
    print(f"sha256={hashlib.sha256(identity).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
