#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
usage:
  wrap_signed_mcu_image_swu_wsl.sh --image /abs/image.bin --version VERSION \
      --target-slot 0|1 --swu-key /secure/private.pem \
      --swu-public-key /safe/public.pem --out-dir /abs/new-release-dir

Wrap an already MCUboot-signed complete-slot image in a signed SWUpdate
remote-handler artifact. The caller remains responsible for validating the
result with the target's official swupdate binary when no native host checker
is available.
EOF
}

fail() { printf '%s\n' "wrap-signed-mcu-swu: $*" >&2; exit 1; }

require_regular_file() {
    [[ -f "$2" && ! -L "$2" ]] || fail "$1 is not a regular file: $2"
}

require_private_key() {
    local owner mode
    require_regular_file "SWU signing key" "$1"
    owner="$(stat -c '%u' -- "$1")"
    mode="$(stat -c '%a' -- "$1")"
    [[ "$owner" == "$(id -u)" && $((8#${mode} & 0077)) -eq 0 ]] ||
        fail "SWU signing key must be owned by the invoking user and not group/world readable"
}

image=''
version=''
target_slot=''
swu_key=''
swu_public_key=''
out_dir=''

while (($#)); do
    case "$1" in
        --image) image="${2:?missing --image value}"; shift 2 ;;
        --version) version="${2:?missing --version value}"; shift 2 ;;
        --target-slot) target_slot="${2:?missing --target-slot value}"; shift 2 ;;
        --swu-key) swu_key="${2:?missing --swu-key value}"; shift 2 ;;
        --swu-public-key) swu_public_key="${2:?missing --swu-public-key value}"; shift 2 ;;
        --out-dir) out_dir="${2:?missing --out-dir value}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; fail "unknown argument: $1" ;;
    esac
done

[[ -n "$image" && -n "$version" && -n "$target_slot" && -n "$swu_key" &&
   -n "$swu_public_key" && -n "$out_dir" ]] || { usage >&2; fail 'missing required argument'; }
[[ "$target_slot" =~ ^[01]$ ]] || fail 'target slot must be 0 or 1'
[[ "$version" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?(\+[0-9]+)?$ ]] || fail 'invalid MCUboot version'
[[ "$out_dir" == /* && ! -e "$out_dir" ]] || fail 'out-dir must be an unused absolute path'
require_regular_file image "$image"
require_private_key "$swu_key"
require_regular_file "SWU public key" "$swu_public_key"
[[ "$(stat -c '%s' -- "$image")" == 131072 ]] || fail 'image must be one complete 131072-byte slot'
command -v openssl >/dev/null 2>&1 || fail 'openssl is required'
command -v cpio >/dev/null 2>&1 || fail 'cpio is required'

umask 077
mkdir -p -- "$out_dir/stage"
install -m 0400 -- "$image" "$out_dir/stage/image.bin"
image_sha256="$(sha256sum -- "$out_dir/stage/image.bin" | awk '{print $1}')"
swu_path="$out_dir/t527-mcu-slot${target_slot}-v${version}.swu"

printf '%s\n' \
    'software = {' \
    "    version = \"${version}\";" \
    "    description = \"T527 MCU slot${target_slot} image ${version}\";" \
    '    images: (' \
    '        {' \
    '            filename = "image.bin";' \
    '            type = "remote";' \
    '            data = "mcu-v1";' \
    "            sha256 = \"${image_sha256}\";" \
    '        }' \
    '    );' \
    '}' > "$out_dir/stage/sw-description"

openssl dgst -sha256 -sign "$swu_key" -sigopt rsa_padding_mode:pss \
    -sigopt rsa_pss_saltlen:-2 -out "$out_dir/stage/sw-description.sig" \
    "$out_dir/stage/sw-description"
openssl dgst -sha256 -verify "$swu_public_key" \
    -signature "$out_dir/stage/sw-description.sig" \
    -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:-2 \
    "$out_dir/stage/sw-description" >/dev/null
(
    cd -- "$out_dir/stage"
    printf '%s\n' sw-description sw-description.sig image.bin | cpio -o -H crc > "$swu_path"
)
chmod 0644 -- "$swu_path"
printf 'image=%s\nimage_size=%s\nimage_sha256=%s\nswu=%s\nswu_sha256=%s\n' \
    "$image" "$(stat -c '%s' -- "$image")" "$image_sha256" "$swu_path" \
    "$(sha256sum -- "$swu_path" | awk '{print $1}')"
