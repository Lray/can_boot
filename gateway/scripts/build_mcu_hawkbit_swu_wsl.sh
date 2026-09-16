#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repo_root="$(cd -- "${script_dir}/../.." && pwd)"

usage()
{
    cat <<'EOF'
usage:
  build_mcu_hawkbit_swu_wsl.sh --payload /abs/Can.bin --target-slot 0|1 \
      --version MAJOR.MINOR[.REVISION][+BUILD] --security-counter N \
      --mcuboot-key /secure/root-ec-p256.pem --imgtool /path/to/imgtool \
      --swu-key /secure/swu-release-private.pem --swu-public-key /safe/swu-release-public.pem \
      --vendor-id N --product-code N --revision-number N --serial-number N \
      --out-dir /abs/new-release-dir [--swupdate /path/to/swupdate]

This is the only release builder: it delegates MCU image signing to the
repository's canonical MCUboot imgtool wrapper, then follows the official
SWUpdate RSA-PSS + cpio-crc packaging procedure and validates the resulting
SWU with the official swupdate -c checker.
EOF
}

fail()
{
    printf '%s\n' "build-mcu-hawkbit-swu: $*" >&2
    exit 1
}

require_regular_file()
{
    local label="$1" file="$2"

    [[ -f "${file}" && ! -L "${file}" ]] || fail "${label} is not a regular file: ${file}"
}

require_private_key()
{
    local label="$1" file="$2" owner mode

    require_regular_file "${label}" "${file}"
    owner="$(stat -c '%u' -- "${file}")"
    mode="$(stat -c '%a' -- "${file}")"
    [[ "${owner}" == "$(id -u)" && $((8#${mode} & 0077)) -eq 0 ]] ||
        fail "${label} must be owned by the invoking user and not group/world readable"
}

require_u32()
{
    [[ "$2" =~ ^[0-9]+$ && $((10#$2)) -le 4294967295 ]] || fail "$1 must be an unsigned 32-bit integer"
}

write_envelope()
{
    python3 - "$1" "$2" "$vendor_id" "$product_code" "$revision_number" "$serial_number" <<'PY'
import shutil
import struct
import sys

source, destination, *identity = sys.argv[1:]
with open(source, "rb") as image, open(destination, "wb") as artifact:
    artifact.write(struct.pack(">IIII", *(int(value) for value in identity)))
    shutil.copyfileobj(image, artifact)
PY
}

payload=''
target_slot=''
version=''
security_counter=''
mcuboot_key=''
imgtool=''
swu_key=''
swu_public_key=''
out_dir=''
swupdate_bin='swupdate'
vendor_id=''
product_code=''
revision_number=''
serial_number=''

while (($#)); do
    case "$1" in
        --payload) payload="${2:?missing --payload value}"; shift 2 ;;
        --target-slot) target_slot="${2:?missing --target-slot value}"; shift 2 ;;
        --version) version="${2:?missing --version value}"; shift 2 ;;
        --security-counter) security_counter="${2:?missing --security-counter value}"; shift 2 ;;
        --mcuboot-key) mcuboot_key="${2:?missing --mcuboot-key value}"; shift 2 ;;
        --imgtool) imgtool="${2:?missing --imgtool value}"; shift 2 ;;
        --swu-key) swu_key="${2:?missing --swu-key value}"; shift 2 ;;
        --swu-public-key) swu_public_key="${2:?missing --swu-public-key value}"; shift 2 ;;
        --vendor-id) vendor_id="${2:?missing --vendor-id value}"; shift 2 ;;
        --product-code) product_code="${2:?missing --product-code value}"; shift 2 ;;
        --revision-number) revision_number="${2:?missing --revision-number value}"; shift 2 ;;
        --serial-number) serial_number="${2:?missing --serial-number value}"; shift 2 ;;
        --out-dir) out_dir="${2:?missing --out-dir value}"; shift 2 ;;
        --swupdate) swupdate_bin="${2:?missing --swupdate value}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; fail "unknown argument: $1" ;;
    esac
done

[[ -n "${payload}" && -n "${target_slot}" && -n "${version}" && -n "${security_counter}" &&
   -n "${mcuboot_key}" && -n "${imgtool}" && -n "${swu_key}" &&
   -n "${swu_public_key}" && -n "${vendor_id}" && -n "${product_code}" &&
   -n "${revision_number}" && -n "${serial_number}" && -n "${out_dir}" ]] || { usage >&2; fail 'missing required argument'; }
[[ "${target_slot}" =~ ^[01]$ ]] || fail 'target slot must be 0 or 1'
[[ "${security_counter}" =~ ^[0-9]+$ ]] || fail 'security counter must be an unsigned decimal value'
[[ "${version}" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?(\+[0-9]+)?$ ]] || fail 'invalid MCUboot version'
[[ "${out_dir}" == /* ]] || fail 'out-dir must be absolute'
[[ ! -e "${out_dir}" ]] || fail "refusing to overwrite existing output: ${out_dir}"

require_regular_file payload "${payload}"
require_private_key MCUboot-signing-key "${mcuboot_key}"
require_private_key SWU-signing-key "${swu_key}"
require_regular_file SWU-public-key "${swu_public_key}"
require_u32 vendor-id "${vendor_id}"
require_u32 product-code "${product_code}"
require_u32 revision-number "${revision_number}"
require_u32 serial-number "${serial_number}"
command -v openssl >/dev/null 2>&1 || fail 'openssl is required'
command -v cpio >/dev/null 2>&1 || fail 'cpio is required'
command -v "${swupdate_bin}" >/dev/null 2>&1 || fail 'swupdate checker is required'
if [[ "${imgtool}" == */* ]]; then
    [[ -x "${imgtool}" || -f "${imgtool}" ]] || fail "imgtool is unavailable: ${imgtool}"
else
    command -v "${imgtool}" >/dev/null 2>&1 || fail "imgtool is unavailable: ${imgtool}"
fi

umask 077
mkdir -p -- "${out_dir}/mcuboot" "${out_dir}/stage"

python3 "${repo_root}/can/scripts/make_mcu_mcuboot_bundle.py" \
    --payload "${payload}" --target-slot "${target_slot}" --version "${version}" \
    --security-counter "${security_counter}" --key "${mcuboot_key}" --imgtool "${imgtool}" \
    --out-dir "${out_dir}/mcuboot"

readonly image_path="${out_dir}/mcuboot/image.bin"
readonly stage_dir="${out_dir}/stage"
readonly swu_path="${out_dir}/t527-mcu-slot${target_slot}-v${version}.swu"
readonly swu_temp="${swu_path}.partial"

write_envelope "${image_path}" "${stage_dir}/image.bin"
chmod 0400 "${stage_dir}/image.bin"
readonly image_size="$(stat -c '%s' -- "${stage_dir}/image.bin")"
readonly image_sha256="$(sha256sum -- "${stage_dir}/image.bin" | awk '{print $1}')"

printf '%s\n' \
    'software = {' \
    "    version = \"${version}\";" \
    "    description = \"T527 MCU slot${target_slot} OTA image ${version}\";" \
    '    images: (' \
    '        {' \
    '            filename = "image.bin";' \
    '            type = "remote";' \
    '            data = "mcu-v1";' \
    "            sha256 = \"${image_sha256}\";" \
    '        }' \
    '    );' \
    '}' > "${stage_dir}/sw-description"

openssl dgst -sha256 -sign "${swu_key}" -sigopt rsa_padding_mode:pss \
    -sigopt rsa_pss_saltlen:-2 -out "${stage_dir}/sw-description.sig" \
    "${stage_dir}/sw-description"
openssl dgst -sha256 -verify "${swu_public_key}" -signature "${stage_dir}/sw-description.sig" \
    -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:-2 "${stage_dir}/sw-description" >/dev/null
(
    cd -- "${stage_dir}"
    printf '%s\n' sw-description sw-description.sig image.bin | cpio -o -H crc > "${swu_temp}"
)
"${swupdate_bin}" -c -i "${swu_temp}" -k "${swu_public_key}"
mv -- "${swu_temp}" "${swu_path}"
chmod 0644 -- "${swu_path}"

printf '%s\n' \
    "MCU_UPDATE_IMAGE_SIZE=${image_size}" \
    "MCU_UPDATE_IMAGE_SHA256=${image_sha256}" \
    "MCU_UPDATE_SWU=$(basename -- "${swu_path}")" \
    "MCU_UPDATE_SWU_SHA256=$(sha256sum -- "${swu_path}" | awk '{print $1}')" \
    "MCU_UPDATE_VERSION=${version}" > "${out_dir}/release-manifest.env"
chmod 0600 -- "${out_dir}/release-manifest.env"

printf 'image=%s\nimage_size=%s\nimage_sha256=%s\nswu=%s\nswu_sha256=%s\n' \
    "${image_path}" "${image_size}" "${image_sha256}" "${swu_path}" \
    "$(sha256sum -- "${swu_path}" | awk '{print $1}')"
