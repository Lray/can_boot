#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SCRIPT="${PROJECT_DIR}/scripts/verify_raw_can.sh"

grep -q '700' "${SCRIPT}"
grep -q '7E0' "${SCRIPT}"
grep -q '7E8' "${SCRIPT}"
grep -q 'bus-off\|ERROR-PASSIVE\|error-passive' "${SCRIPT}"

if grep -q 'grep "7E0"' "${SCRIPT}"; then
    echo "verify_raw_can.sh must not use local 0x7E0 echo as success criterion" >&2
    exit 1
fi
