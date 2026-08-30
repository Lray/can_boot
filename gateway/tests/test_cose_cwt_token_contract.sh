#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

set +e
python3 "${PROJECT_DIR}/scripts/make_cose_cwt_token.py" \
    --seed-hex A1B2C3D4 \
    --nonce 0x0102030405060708 \
    --out-dir "${TMP_DIR}/contract-only" > "${TMP_DIR}/contract-only.out" 2>&1
rc=$?
set -e
test "${rc}" -eq 3
grep -q 'PENDING: mature COSE/CWT signer not configured' "${TMP_DIR}/contract-only.out"
test -s "${TMP_DIR}/contract-only/cose-cwt-token-contract.json"
grep -q '"serialization": "COSE_Sign1 carrying CWT claims"' "${TMP_DIR}/contract-only/cose-cwt-token-contract.json"
grep -q '"seed_challenge"' "${TMP_DIR}/contract-only/cose-cwt-token-contract.json"
grep -q '"freshness_nonce"' "${TMP_DIR}/contract-only/cose-cwt-token-contract.json"
grep -q '"acceptance_valid": false' "${TMP_DIR}/contract-only/cose-cwt-token-contract.json"

openssl ecparam -name prime256v1 -genkey -noout -out "${TMP_DIR}/dev-private-key.pem"
python3 "${PROJECT_DIR}/scripts/make_cose_cwt_token.py" \
    --dev-private-key-pem "${TMP_DIR}/dev-private-key.pem" \
    --seed-hex A1B2C3D4 \
    --nonce 0x0102030405060708 \
    --out-dir "${TMP_DIR}/tokens" \
    --negative-dir "${TMP_DIR}/negative"

test -s "${TMP_DIR}/tokens/token.hex"
test -s "${TMP_DIR}/tokens/token.cose"
test -s "${TMP_DIR}/tokens/claims.cbor"
test -s "${TMP_DIR}/tokens/claims.json"
test -s "${TMP_DIR}/tokens/dev-public-key.pem"
test -s "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"acceptance_valid": true' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"serialization": "COSE_Sign1 carrying CWT claims"' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"signature_algorithm": "ES256"' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"protected_header"' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"claim_label_map"' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"seed_challenge": -70001' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"freshness_nonce": -70008' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"
grep -q '"token_file_format": "hex COSE_Sign1/CWT token payload for UDS 27 02, excluding SID/subfunction"' "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"

python3 "${PROJECT_DIR}/scripts/make_cose_cwt_token.py" \
    verify \
    --token "${TMP_DIR}/tokens/token.cose" \
    --public-key-pem "${TMP_DIR}/tokens/dev-public-key.pem" \
    --manifest "${TMP_DIR}/tokens/cose-cwt-token-manifest.json"

for fixture in wrong_seed bad_signature replay_old_nonce; do
    test -s "${TMP_DIR}/negative/${fixture}/token.hex"
    test -s "${TMP_DIR}/negative/${fixture}/token.cose"
    test -s "${TMP_DIR}/negative/${fixture}/cose-cwt-token-manifest.json"
    grep -q "\"negative_case\": \"${fixture}\"" "${TMP_DIR}/negative/${fixture}/cose-cwt-token-manifest.json"
done

if grep -R -q 'PRIVATE KEY' "${TMP_DIR}/tokens" "${TMP_DIR}/negative"; then
    echo "COSE/CWT fixture outputs must not contain raw signing key material" >&2
    exit 1
fi
