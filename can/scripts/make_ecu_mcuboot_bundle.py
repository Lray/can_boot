#!/usr/bin/env python3
import argparse
import hashlib
import pathlib
import struct
import subprocess

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

SLOT_SIZE = 0x20000
HEADER_SIZE = 0x200
TRAILER_OFFSET = 0x1FFC0
ALIGNMENT = 16
SWAP_INFO_OFFSET = TRAILER_OFFSET
COPY_DONE_OFFSET = TRAILER_OFFSET + 0x10
IMAGE_OK_OFFSET = TRAILER_OFFSET + 0x20
MAGIC_OFFSET = TRAILER_OFFSET + 0x30
BOOT_MAGIC = bytes([
    0x10, 0x00, 0x2D, 0xE1, 0x5D, 0x29, 0x41, 0x0B,
    0x8D, 0x77, 0x67, 0x9C, 0x11, 0x0F, 0x1F, 0x8A,
])
BOOT_FLAG_SET = 0x01

TRAILER_ERASED = "erased"
TRAILER_PENDING = "pending"
TRAILER_CONFIRMED = "confirmed"

SLOT_PAYLOAD_BASE = {
    0: 0x08010200,
    1: 0x08030200,
}

STM32U5_SRAM_RANGES = (
    (0x20000000, 0x000C0000),
    (0x200C0000, 0x00010000),
    (0x200D0000, 0x000D0000),
    (0x28000000, 0x00004000),
    (0x201A0000, 0x000D0000),
)

IMAGE_MAGIC = 0x96F3B83D
IMAGE_TLV_INFO_MAGIC = 0x6907
IMAGE_TLV_PROT_INFO_MAGIC = 0x6908
IMAGE_TLV_KEYHASH = 0x0001
IMAGE_TLV_SHA256 = 0x0010
IMAGE_TLV_ECDSA_SIG = 0x0022
IMAGE_TLV_SEC_CNT = 0x0050


def payload_copy_name(target_slot: int) -> str:
    return "payload-primary.bin" if target_slot == 0 else "payload-secondary.bin"


def default_payload_path() -> pathlib.Path:
    return REPO_ROOT / "build" / "Debug-slot0" / "Can.bin"


def parse_u32(value: str) -> int:
    parsed = int(value, 0)
    if parsed < 0 or parsed > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"{value} is outside uint32 range")
    return parsed


def parse_version(value: str) -> tuple[int, int, int, int]:
    parts = value.split("+", 1)
    core = parts[0].split(".")
    if len(core) not in (2, 3):
        raise argparse.ArgumentTypeError("version must be MAJOR.MINOR[.REVISION][+BUILD]")
    major = int(core[0], 0)
    minor = int(core[1], 0)
    revision = int(core[2], 0) if len(core) == 3 else 0
    build = int(parts[1], 0) if len(parts) == 2 else 0
    if not (0 <= major <= 0xFF and 0 <= minor <= 0xFF and 0 <= revision <= 0xFFFF):
        raise argparse.ArgumentTypeError("version major/minor/revision is out of MCUboot range")
    if not (0 <= build <= 0xFFFFFFFF):
        raise argparse.ArgumentTypeError("version build number is out of MCUboot range")
    return major, minor, revision, build


def version_string(version: tuple[int, int, int, int]) -> str:
    major, minor, revision, build = version
    if build:
        return f"{major}.{minor}.{revision}+{build}"
    return f"{major}.{minor}.{revision}"


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def is_boot_valid_stack_pointer(stack_pointer: int) -> bool:
    if (stack_pointer & 0x7) != 0:
        return False

    return any(
        base <= stack_pointer < (base + size)
        for base, size in STM32U5_SRAM_RANGES
    )


def validate_payload(payload: bytes, target_slot: int) -> None:
    if len(payload) < 8:
        raise ValueError("payload is too small to contain a vector table")
    if len(payload) > (TRAILER_OFFSET - HEADER_SIZE):
        raise ValueError("payload leaves no room for MCUboot TLV before trailer")

    stack_pointer = read_u32(payload, 0)
    reset_handler = read_u32(payload, 4) & 0xFFFFFFFE
    base = SLOT_PAYLOAD_BASE[target_slot]
    end = base + len(payload)

    if not is_boot_valid_stack_pointer(stack_pointer):
        raise ValueError(
            f"payload vector SP 0x{stack_pointer:08X} is outside Boot-valid "
            "STM32U5 SRAM"
        )
    if not (base <= reset_handler < end):
        raise ValueError(
            f"payload reset vector 0x{reset_handler:08X} is not within target slot "
            f"payload range 0x{base:08X}..0x{end - 1:08X}; rebuild for target slot {target_slot}"
        )


def prepare_payload_input(args: argparse.Namespace) -> pathlib.Path:
    if args.payload is not None:
        return args.payload

    payload = default_payload_path()
    if not payload.is_file():
        raise FileNotFoundError(
            f"CMake slot 0 payload not found: {payload}; "
            "run cmake --preset Debug-slot0 and cmake --build --preset Debug-slot0"
        )
    return payload


def pad_image_to_slot(image: bytes) -> bytes:
    if len(image) > TRAILER_OFFSET:
        raise ValueError(f"signed image size 0x{len(image):X} reaches trailer boundary 0x{TRAILER_OFFSET:X}")
    body_padding = b"\xFF" * (TRAILER_OFFSET - len(image))
    trailer_padding = b"\xFF" * (SLOT_SIZE - TRAILER_OFFSET)
    return image + body_padding + trailer_padding


def build_trailer(state: str) -> bytes:
    if state not in (TRAILER_ERASED, TRAILER_PENDING, TRAILER_CONFIRMED):
        raise ValueError(f"unsupported trailer state: {state}")

    trailer = bytearray(b"\xFF" * (SLOT_SIZE - TRAILER_OFFSET))
    if state in (TRAILER_PENDING, TRAILER_CONFIRMED):
        magic_offset = MAGIC_OFFSET - TRAILER_OFFSET
        trailer[magic_offset:magic_offset + len(BOOT_MAGIC)] = BOOT_MAGIC
    if state == TRAILER_CONFIRMED:
        trailer[COPY_DONE_OFFSET - TRAILER_OFFSET] = BOOT_FLAG_SET
        trailer[IMAGE_OK_OFFSET - TRAILER_OFFSET] = BOOT_FLAG_SET
    return bytes(trailer)


def apply_pending_trailer(image: bytes) -> bytes:
    if len(image) != SLOT_SIZE:
        raise ValueError("pending trailer requires a full-slot image")
    return image[:TRAILER_OFFSET] + build_trailer(TRAILER_PENDING)


def apply_confirmed_trailer(image: bytes) -> bytes:
    if len(image) != SLOT_SIZE:
        raise ValueError("confirmed trailer requires a full-slot image")
    return image[:TRAILER_OFFSET] + build_trailer(TRAILER_CONFIRMED)


def validate_slot_trailer(image: bytes, expected_state: str) -> None:
    if len(image) != SLOT_SIZE:
        raise ValueError("trailer validation requires a full-slot image")
    if image[TRAILER_OFFSET:] != build_trailer(expected_state):
        raise ValueError(
            f"slot trailer does not match required {expected_state} state"
        )


def run_imgtool(args: argparse.Namespace, version: tuple[int, int, int, int]) -> None:
    compact_image = args.out_dir / "image.compact.bin"
    cmd = [
        args.imgtool,
        "sign",
        "--sha", "256",
        "--header-size", f"0x{HEADER_SIZE:X}",
        "--pad-header",
        "--align", str(ALIGNMENT),
        "--version", version_string(version),
        "--slot-size", f"0x{SLOT_SIZE:X}",
        "--key", str(args.key),
    ]
    if args.security_counter is not None:
        cmd.extend(["--security-counter", str(args.security_counter)])
    cmd.extend([str(args.payload), str(compact_image)])
    subprocess.run(cmd, check=True)


def validate_signed_image(image: bytes,
                          expected_version: tuple[int, int, int, int],
                          expected_trailer_state: str = TRAILER_ERASED,
                          expected_security_counter: int | None = None) -> None:
    if len(image) != SLOT_SIZE and len(image) >= TRAILER_OFFSET:
        raise ValueError(f"signed image size 0x{len(image):X} reaches trailer boundary 0x{TRAILER_OFFSET:X}")
    if len(image) == SLOT_SIZE:
        validate_slot_trailer(image, expected_trailer_state)
    if len(image) < HEADER_SIZE + 8:
        raise ValueError("signed image is too small")

    magic = read_u32(image, 0)
    header_size = read_u16(image, 8)
    protected_tlv_size = read_u16(image, 10)
    image_size = read_u32(image, 12)
    flags = read_u32(image, 16)
    major = image[20]
    minor = image[21]
    revision = read_u16(image, 22)
    build = read_u32(image, 24)

    if magic != IMAGE_MAGIC:
        raise ValueError(f"bad image magic 0x{magic:08X}")
    if header_size != HEADER_SIZE:
        raise ValueError(f"unexpected header size 0x{header_size:X}")
    if flags & 0x10:
        raise ValueError("image is marked non-bootable")
    if (major, minor, revision, build) != expected_version:
        raise ValueError("signed image header version does not match requested version")

    tlv_info_offset = header_size + image_size
    if tlv_info_offset + protected_tlv_size >= TRAILER_OFFSET:
        raise ValueError("image payload/protected TLV reaches trailer boundary")

    magic_tlv = read_u16(image, tlv_info_offset)
    unprotected_info_offset = tlv_info_offset
    security_counter_seen = False
    if magic_tlv == IMAGE_TLV_PROT_INFO_MAGIC:
        total = read_u16(image, tlv_info_offset + 2)
        if total != protected_tlv_size:
            raise ValueError("protected TLV total does not match header")
        offset = tlv_info_offset + 4
        protected_tlv_end = tlv_info_offset + protected_tlv_size
        while offset < protected_tlv_end:
            tlv_type = read_u16(image, offset)
            tlv_len = read_u16(image, offset + 2)
            offset += 4
            if offset + tlv_len > protected_tlv_end:
                raise ValueError("protected TLV entry exceeds protected TLV area")
            if tlv_type == IMAGE_TLV_SEC_CNT:
                if tlv_len != 4:
                    raise ValueError("IMAGE_TLV_SEC_CNT length is not 4")
                security_counter = read_u32(image, offset)
                if expected_security_counter is not None and security_counter != expected_security_counter:
                    raise ValueError(
                        f"IMAGE_TLV_SEC_CNT is {security_counter}, expected {expected_security_counter}"
                    )
                security_counter_seen = True
            offset += tlv_len
        unprotected_info_offset = tlv_info_offset + protected_tlv_size
        magic_tlv = read_u16(image, unprotected_info_offset)
    elif protected_tlv_size != 0:
        raise ValueError("header declares protected TLV but signed image lacks protected TLV info")
    elif expected_security_counter is not None:
        raise ValueError("signed image is missing protected IMAGE_TLV_SEC_CNT")

    if expected_security_counter is not None and not security_counter_seen:
        raise ValueError("signed image is missing protected IMAGE_TLV_SEC_CNT")

    if magic_tlv != IMAGE_TLV_INFO_MAGIC:
        raise ValueError(f"standard TLV magic is 0x{magic_tlv:04X}, expected 0x6907")

    tlv_total = read_u16(image, unprotected_info_offset + 2)
    tlv_end = unprotected_info_offset + tlv_total
    if tlv_end > len(image) or tlv_end >= TRAILER_OFFSET:
        raise ValueError("standard TLV reaches outside signed image or trailer boundary")

    offset = unprotected_info_offset + 4
    seen = set()
    while offset < tlv_end:
        tlv_type = read_u16(image, offset)
        tlv_len = read_u16(image, offset + 2)
        offset += 4
        if offset + tlv_len > tlv_end:
            raise ValueError("TLV entry exceeds TLV area")
        seen.add(tlv_type)
        offset += tlv_len

    missing = [name for value, name in (
        (IMAGE_TLV_KEYHASH, "KEYHASH"),
        (IMAGE_TLV_SHA256, "SHA256"),
        (IMAGE_TLV_ECDSA_SIG, "ECDSA_SIG"),
    ) if value not in seen]
    if missing:
        raise ValueError(f"signed image is missing required TLV(s): {', '.join(missing)}")
    if (len(image) == SLOT_SIZE and
            image[tlv_end:TRAILER_OFFSET] != (b"\xFF" * (TRAILER_OFFSET - tlv_end))):
        raise ValueError("full-slot body padding must remain erased 0xFF")


def write_info(args: argparse.Namespace,
               version: tuple[int, int, int, int],
               image_sha256: bytes) -> None:
    image_path = args.out_dir / "image.bin"
    payload_base = SLOT_PAYLOAD_BASE[args.target_slot]
    copied_payload = payload_copy_name(args.target_slot)
    (args.out_dir / "package.info").write_text(
        "\n".join([
            "source=ecu-boot-valid-target-runtime",
            "boot_valid=yes",
            f"target_slot={args.target_slot}",
            f"payload_base=0x{payload_base:08X}",
            f"header_size=0x{HEADER_SIZE:X}",
            f"slot_size=0x{SLOT_SIZE:X}",
            f"trailer_offset=0x{TRAILER_OFFSET:X}",
            f"trailer_state={TRAILER_PENDING}",
            f"image_size={image_path.stat().st_size}",
            f"image_version={version_string(version)}",
            f"security_counter={args.security_counter if args.security_counter is not None else 'none'}",
            f"image_sha256={image_sha256.hex()}",
            f"payload_input={args.payload}",
            f"payload_copy={args.out_dir / copied_payload}",
            f"key={args.key}",
            "",
        ]),
        encoding="ascii",
    )
    (args.out_dir / "package.source").write_text(
        "\n".join([
            "source=external-boot-valid-input",
            "boot_valid=yes",
            f"image={image_path}",
            "",
        ]),
        encoding="ascii",
    )
    (args.out_dir / "package.sha256").write_text(
        f"{image_sha256.hex()}  image.bin\n",
        encoding="ascii",
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create a signed ECU MCUboot image and OTA or provisioning bundle."
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--payload", type=pathlib.Path,
                        help="raw app payload linked to target slot payload base, without MCUboot header")
    parser.add_argument("--out-dir", type=pathlib.Path, default=pathlib.Path("build-target"))
    parser.add_argument("--target-slot", type=int, choices=(0, 1), default=0)
    parser.add_argument("--version", default="1.2.0",
                        help="MCUboot image version (major.minor[.revision][+build])")
    parser.add_argument("--security-counter", type=parse_u32,
                        help="MCUboot protected IMAGE_TLV_SEC_CNT rollback counter")
    parser.add_argument("--key", type=pathlib.Path,
                        default=pathlib.Path(r"E:\Simple_ST\Boot\keys\root-ec-p256.pem"))
    parser.add_argument("--imgtool", default="imgtool.exe")
    parser.add_argument("--provisioning", action="store_true",
                        help="emit a confirmed-trailer image.bin for Boot YMODEM initial "
                             "provisioning instead of a Gateway OTA package input")
    args = parser.parse_args()

    if not args.key.is_file():
        raise SystemExit(f"Boot signing key not found: {args.key}")

    if args.provisioning and args.security_counter is None:
        raise SystemExit(
            "--provisioning requires --security-counter; "
            "the STM32U5 Boot image validator has hardware rollback protection "
            "enabled and rejects images without protected IMAGE_TLV_SEC_CNT"
        )

    version = parse_version(args.version)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    try:
        payload_path = prepare_payload_input(args)
    except (FileNotFoundError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc

    if not payload_path.is_file():
        raise SystemExit(f"payload not found: {payload_path}")

    args.payload = payload_path
    payload = payload_path.read_bytes()
    validate_payload(payload, args.target_slot)
    copied_payload = args.out_dir / payload_copy_name(args.target_slot)
    if not args.provisioning and payload_path.resolve() != copied_payload.resolve():
        copied_payload.write_bytes(payload)

    run_imgtool(args, version)

    image_path = args.out_dir / "image.bin"
    compact_image_path = args.out_dir / "image.compact.bin"
    compact_image = compact_image_path.read_bytes()
    validate_signed_image(compact_image, version, expected_security_counter=args.security_counter)
    image = pad_image_to_slot(compact_image)
    if args.provisioning:
        image = apply_confirmed_trailer(image)
        trailer_state = TRAILER_CONFIRMED
    else:
        image = apply_pending_trailer(image)
        trailer_state = TRAILER_PENDING
    image_path.write_bytes(image)
    validate_signed_image(
        image,
        version,
        expected_trailer_state=trailer_state,
        expected_security_counter=args.security_counter,
    )
    compact_image_path.unlink(missing_ok=True)
    image_sha256 = hashlib.sha256(image).digest()

    if not args.provisioning:
        write_info(args, version, image_sha256)

    print(f"wrote {args.out_dir / 'image.bin'}")
    print(f"image_size={len(image)}")
    print(f"image_version={version_string(version)}")
    print(f"image_sha256={image_sha256.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
