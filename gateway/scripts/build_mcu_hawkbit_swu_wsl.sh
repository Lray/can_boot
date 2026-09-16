#!/usr/bin/env bash
set -euo pipefail

usage()
{
    cat <<'EOF'
usage:
  build_mcu_hawkbit_swu_wsl.sh --image /abs/image.bin \
      --target-identity VVVVVVVV:PPPPPPPP:RRRRRRRR:SSSSSSSS \
      --version VERSION --swu-key /mnt/e/T527/can_boot/key/private.pem \
      --swu-public-key /mnt/e/T527/can_boot/key/public.pem \
      --out-dir /abs/new-release-dir [--swupdate /path/to/swupdate]

The input is an already signed MCUboot image produced only by
E:\T527\can_boot\tools\image.py. swugenerator 0.6 creates the signed SWU.
EOF
}

fail() { printf '%s\n' "build-mcu-hawkbit-swu: $*" >&2; exit 1; }
require_file() { [[ -f "$2" && ! -L "$2" ]] || fail "$1 is not a regular file: $2"; }

image=''
target_identity=''
version=''
swu_key=''
swu_public_key=''
out_dir=''
swupdate_bin='swupdate'

while (($#)); do
    case "$1" in
        --image) image="${2:?missing --image value}"; shift 2 ;;
        --target-identity) target_identity="${2:?missing --target-identity value}"; shift 2 ;;
        --version) version="${2:?missing --version value}"; shift 2 ;;
        --swu-key) swu_key="${2:?missing --swu-key value}"; shift 2 ;;
        --swu-public-key) swu_public_key="${2:?missing --swu-public-key value}"; shift 2 ;;
        --out-dir) out_dir="${2:?missing --out-dir value}"; shift 2 ;;
        --swupdate) swupdate_bin="${2:?missing --swupdate value}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; fail "unknown argument: $1" ;;
    esac
done

[[ -n "$image" && -n "$target_identity" && -n "$version" && -n "$swu_key" &&
   -n "$swu_public_key" && -n "$out_dir" ]] || { usage >&2; fail 'missing required argument'; }
[[ "$target_identity" =~ ^[0-9A-F]{8}:[0-9A-F]{8}:[0-9A-F]{8}:[0-9A-F]{8}$ ]] ||
    fail 'target identity must be four uppercase eight-digit hexadecimal fields'
[[ "$version" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?(\+[0-9]+)?$ ]] || fail 'invalid version'
[[ "$out_dir" == /* && ! -e "$out_dir" ]] || fail 'out-dir must be an unused absolute path'
case "$swu_key" in /mnt/e/T527/can_boot/key/*) ;; *) fail 'SWU key must come from /mnt/e/T527/can_boot/key' ;; esac
case "$swu_public_key" in /mnt/e/T527/can_boot/key/*) ;; *) fail 'SWU public key must come from /mnt/e/T527/can_boot/key' ;; esac
case "$swu_key" in *','*) fail 'SWU key path contains an unsupported comma' ;; esac
require_file image "$image"
require_file SWU-signing-key "$swu_key"
require_file SWU-public-key "$swu_public_key"
[[ "$(stat -c '%u' -- "$swu_key")" == "$(id -u)" &&
   $((8#$(stat -c '%a' -- "$swu_key") & 0077)) -eq 0 ]] ||
    fail 'SWU signing key must be private to the invoking user'
command -v swugenerator >/dev/null 2>&1 || fail 'swugenerator 0.6 is required'
command -v "$swupdate_bin" >/dev/null 2>&1 || fail 'official swupdate checker is required'
command -v openssl >/dev/null 2>&1 || fail 'openssl is required'

endpoint_identity="${target_identity//:/-}"
umask 077
mkdir -p -- "$out_dir/stage"
install -m 0400 -- "$image" "$out_dir/stage/image.bin"
swu_path="$out_dir/mcu-${endpoint_identity}-v${version}.swu"

printf '%s\n' \
    'software = {' \
    "    version = \"$version\";" \
    '    images: (' \
    '        {' \
    '            filename = "image.bin";' \
    '            type = "remote";' \
    "            data = \"mcu-v1-${endpoint_identity}\";" \
    '        }' \
    '    );' \
    '}' > "$out_dir/sw-description"

sign_command='exec openssl dgst -sha256 -sign "$1" -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:-2 -out "$3" "$2"'
swugenerator -s "$out_dir/sw-description" -a "$out_dir/stage" -o "$swu_path" \
    -k "CUSTOM,bash,-c,$sign_command,--,$swu_key" create
"$swupdate_bin" -c -i "$swu_path" -k "$swu_public_key"
chmod 0644 -- "$swu_path"

printf 'image=%s\ntarget_identity=%s\nswu=%s\nswu_sha256=%s\n' \
    "$image" "$target_identity" "$swu_path" "$(sha256sum -- "$swu_path" | awk '{print $1}')"
