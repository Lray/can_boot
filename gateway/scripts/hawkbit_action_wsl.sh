#!/usr/bin/env bash
set -euo pipefail

readonly MANAGEMENT_URL="${HAWKBIT_MANAGEMENT_URL:-http://127.0.0.1:18080}"
readonly CREDENTIAL_FILE="${HAWKBIT_CREDENTIAL_FILE:-${HOME}/.config/ecu-ota/hawkbit-admin.env}"
readonly STATE_DIRECTORY="${HAWKBIT_STATE_DIRECTORY:-${HOME}/.local/state/ecu-ota-hawkbit}"
usage()
{
    cat <<'EOF'
usage:
  hawkbit_action_wsl.sh provision --hawkbit-config /secure/hawkbit.conf \
      --target-address IPv4 [--state ~/.local/state/ecu-ota-hawkbit/action.env]
  hawkbit_action_wsl.sh register --hawkbit-host IPv4 --target-address IPv4 \
      [--state ~/.local/state/ecu-ota-hawkbit/action.env]
  hawkbit_action_wsl.sh assign --artifact /mnt/e/.../gateway-hawkbit-e2e-v101.swu \
      --state ~/.local/state/ecu-ota-hawkbit/action.env
  hawkbit_action_wsl.sh status --state ~/.local/state/ecu-ota-hawkbit/action.env

The credential file must be owned by the current WSL user, mode 0600 or 0400,
and define HAWKBIT_ADMIN_USER and HAWKBIT_ADMIN_PASSWORD.

provision reads the fixed board-side HAWKBIT_SERVER_URL, HAWKBIT_TENANT,
HAWKBIT_TARGET_ID and HAWKBIT_TARGET_TOKEN values from an owner-only copy of
hawkbit.conf. It creates that stable target once; assign then uploads and
assigns one SWU without creating a second target.
EOF
}

fail()
{
    printf '%s\n' "$*" >&2
    exit 1
}

require_safe_file()
{
    local file="$1"
    local mode owner

    test -f "${file}" || fail "Required file does not exist: ${file}"
    test ! -L "${file}" || fail "Refusing symbolic link: ${file}"
    mode="$(stat -c '%a' "${file}")"
    owner="$(stat -c '%u' "${file}")"
    test "${owner}" = "$(id -u)" || fail "File owner is unsafe: ${file}"
    (( (8#${mode} & 0077) == 0 )) || fail "File permissions are unsafe: ${file}"
}

load_credentials()
{
    require_safe_file "${CREDENTIAL_FILE}"
    # The credential file is locally provisioned and owner-only.
    # shellcheck disable=SC1090
    source "${CREDENTIAL_FILE}"
    : "${HAWKBIT_ADMIN_USER:?HAWKBIT_ADMIN_USER is required}"
    : "${HAWKBIT_ADMIN_PASSWORD:?HAWKBIT_ADMIN_PASSWORD is required}"
}

load_hawkbit_target_config()
{
    local config_file="$1"

    require_safe_file "${config_file}"
    # The root-only board configuration is explicitly copied to an owner-only
    # WSL file for one-time target provisioning; it is never printed.
    # shellcheck disable=SC1090
    source "${config_file}"
    : "${HAWKBIT_SERVER_URL:?HAWKBIT_SERVER_URL is required}"
    : "${HAWKBIT_TENANT:?HAWKBIT_TENANT is required}"
    : "${HAWKBIT_TARGET_ID:?HAWKBIT_TARGET_ID is required}"
    : "${HAWKBIT_TARGET_TOKEN:?HAWKBIT_TARGET_TOKEN is required}"
    case "${HAWKBIT_SERVER_URL}" in
        http://*|https://*) ;;
        *) fail 'HAWKBIT_SERVER_URL must use http or https' ;;
    esac
    case "${HAWKBIT_TARGET_ID}" in ''|*[!A-Za-z0-9._-]*) fail 'Invalid target ID' ;; esac
    [[ "${HAWKBIT_TARGET_TOKEN}" =~ ^[0-9a-f]{32}$ ]] || fail 'Invalid target token'
    case "${HAWKBIT_TENANT}" in ''|*[!A-Za-z0-9._-]*) fail 'Invalid tenant' ;; esac
}

curl_api()
{
    curl --fail --silent --show-error --max-time 15 \
        --user "${HAWKBIT_ADMIN_USER}:${HAWKBIT_ADMIN_PASSWORD}" \
        --header 'Accept: application/json' "$@"
}

wait_for_api()
{
    local attempt

    for attempt in $(seq 1 30); do
        if curl_api "${MANAGEMENT_URL}/rest/v1/targets" >/dev/null 2>&1 &&
           curl_api "${MANAGEMENT_URL}/rest/v1/targets" >/dev/null 2>&1; then
            return 0
        fi
        sleep 2
    done
    fail "hawkBit Management API did not become stable"
}

json_target()
{
    python3 - "$1" "$2" "$3" <<'PY'
import json
import sys

target_id, target_address, token = sys.argv[1:]
print(json.dumps([{
    "controllerId": target_id,
    "name": "T527 real Gateway delivery",
    "description": "Official Suricatta and Remote Handler delivery; MCU execution disabled.",
    "address": "http://" + target_address,
    "securityToken": token,
    "requestAttributes": True,
}], separators=(",", ":")))
PY
}

json_module()
{
    python3 - "$1" <<'PY'
import json
import sys

target_id = sys.argv[1]
print(json.dumps([{
    "name": "T527 Gateway SWU " + target_id,
    "version": "2026.08.13.1",
    "type": "os",
    "description": "RSA-PSS signed SWUpdate artifact for the real Gateway bridge.",
}], separators=(",", ":")))
PY
}

json_distribution_set()
{
    python3 - "$1" "$2" <<'PY'
import json
import sys

target_id, module_id = sys.argv[1:]
print(json.dumps([{
    "name": "T527 Gateway delivery " + target_id,
    "version": "2026.08.13.1",
    "description": "One-time official hawkBit deployment to the real Gateway bridge.",
    "modules": [{"id": int(module_id)}],
    "type": "os",
}], separators=(",", ":")))
PY
}

json_assignment()
{
    python3 - "$1" <<'PY'
import json
import sys

print(json.dumps([{"id": int(sys.argv[1]), "type": "forced"}], separators=(",", ":")))
PY
}

first_id()
{
    python3 -c 'import json, sys; print(json.load(sys.stdin)[0]["id"])'
}

verify_artifact()
{
    local expected_size="$1"
    local expected_sha1="$2"
    local expected_sha256="$3"

    python3 -c '
import json
import sys

artifact = json.load(sys.stdin)
size, sha1, sha256 = sys.argv[1:]
if (artifact.get("size") != int(size) or
        artifact.get("hashes", {}).get("sha1") != sha1 or
        artifact.get("hashes", {}).get("sha256") != sha256):
    raise SystemExit("hawkBit artifact checksum or size mismatch")
' "${expected_size}" "${expected_sha1}" "${expected_sha256}"
}

latest_action_id()
{
    python3 -c 'import json, sys; actions = json.load(sys.stdin)["content"]; print(max(action["id"] for action in actions))'
}

write_state()
{
    local state_file="$1"
    local temporary

    install -d -m 0700 "${STATE_DIRECTORY}"
    temporary="${state_file}.tmp.$$"
    umask 077
    {
        printf 'DEVICE_URL=%q\n' "${DEVICE_URL}"
        printf 'TARGET_ID=%q\n' "${TARGET_ID}"
        printf 'TARGET_TOKEN=%q\n' "${TARGET_TOKEN}"
        test -z "${ACTION_ID:-}" || printf 'ACTION_ID=%q\n' "${ACTION_ID}"
        test -z "${ARTIFACT_SHA256:-}" || printf 'ARTIFACT_SHA256=%q\n' "${ARTIFACT_SHA256}"
    } > "${temporary}"
    chmod 0600 "${temporary}"
    mv -f "${temporary}" "${state_file}"
}

load_state()
{
    local state_file="$1"

    require_safe_file "${state_file}"
    # The state file is generated by write_state and owner-only.
    # shellcheck disable=SC1090
    source "${state_file}"
    : "${DEVICE_URL:?Missing DEVICE_URL in state}"
    : "${TARGET_ID:?Missing TARGET_ID in state}"
    : "${TARGET_TOKEN:?Missing TARGET_TOKEN in state}"
}

print_status()
{
    local action="" target

    target="$(curl_api "${MANAGEMENT_URL}/rest/v1/targets/${TARGET_ID}")"
    if test -n "${ACTION_ID:-}"; then
        action="$(curl_api "${MANAGEMENT_URL}/rest/v1/targets/${TARGET_ID}/actions/${ACTION_ID}")"
    fi
    python3 - "${action}" "${target}" <<'PY'
import json
import sys

action = json.loads(sys.argv[1]) if sys.argv[1] else None
target = json.loads(sys.argv[2])
print("TARGET_ID=" + target["controllerId"])
print("TARGET_UPDATE_STATUS=" + target["updateStatus"])
print("TARGET_LAST_CONTROLLER_REQUEST_AT=" + str(target.get("lastControllerRequestAt", "")))
if action:
    print("ACTION_ID=" + str(action["id"]))
    print("ACTION_STATUS=" + action["status"])
    print("ACTION_ACTIVE=" + str(action["active"]).lower())
PY
}

register_target()
{
    local hawkbit_host="$1"
    local target_address="$2"
    local state_file="$3"
    local target_response

    [[ "${hawkbit_host}" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]] || fail "Invalid hawkBit IPv4 address"
    [[ "${target_address}" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]] || fail "Invalid target IPv4 address"
    [[ "${state_file}" == "${STATE_DIRECTORY}/"* ]] || fail "State path must be below ${STATE_DIRECTORY}"
    test ! -e "${state_file}" || fail "State already exists: ${state_file}"

    load_credentials
    wait_for_api
    TARGET_ID="t527-hawkbit-gateway-$(uuidgen | tr '[:upper:]' '[:lower:]')"
    TARGET_TOKEN="$(openssl rand -hex 16)"
    DEVICE_URL="http://${hawkbit_host}:18080"
    ACTION_ID=""
    ARTIFACT_SHA256=""

    target_response="$(json_target "${TARGET_ID}" "${target_address}" "${TARGET_TOKEN}" | \
        curl_api --request POST --header 'Content-Type: application/json' --data-binary @- \
        "${MANAGEMENT_URL}/rest/v1/targets")"
    test "$(printf '%s' "${target_response}" | python3 -c 'import json, sys; print(json.load(sys.stdin)[0]["controllerId"])')" = "${TARGET_ID}" ||
        fail "hawkBit target response did not match the requested target"

    write_state "${state_file}"
    printf 'STATE_FILE=%s\nTARGET_ID=%s\n' "${state_file}" "${TARGET_ID}"
}

provision_target()
{
    local config_file="$1"
    local target_address="$2"
    local state_file="$3"
    local target_response

    [[ "${target_address}" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]] || fail "Invalid target IPv4 address"
    [[ "${state_file}" == "${STATE_DIRECTORY}/"* ]] || fail "State path must be below ${STATE_DIRECTORY}"
    test ! -e "${state_file}" || fail "State already exists: ${state_file}"
    load_credentials
    load_hawkbit_target_config "${config_file}"
    wait_for_api
    TARGET_ID="${HAWKBIT_TARGET_ID}"
    TARGET_TOKEN="${HAWKBIT_TARGET_TOKEN}"
    DEVICE_URL="${HAWKBIT_SERVER_URL}"
    ACTION_ID=""
    ARTIFACT_SHA256=""

    target_response="$(json_target "${TARGET_ID}" "${target_address}" "${TARGET_TOKEN}" | \
        curl_api --request POST --header 'Content-Type: application/json' --data-binary @- \
        "${MANAGEMENT_URL}/rest/v1/targets")"
    test "$(printf '%s' "${target_response}" | python3 -c 'import json, sys; print(json.load(sys.stdin)[0]["controllerId"])')" = "${TARGET_ID}" ||
        fail "hawkBit target response did not match the fixed target"

    write_state "${state_file}"
    printf 'STATE_FILE=%s\nTARGET_ID=%s\n' "${state_file}" "${TARGET_ID}"
}

assign_action()
{
    local artifact="$1"
    local state_file="$2"
    local module_response artifact_response distribution_response assignment_response
    local module_id distribution_id

    test -f "${artifact}" || fail "Artifact does not exist: ${artifact}"
    test ! -L "${artifact}" || fail "Refusing symbolic link artifact: ${artifact}"
    load_credentials
    load_state "${state_file}"
    test -z "${ACTION_ID:-}" || fail "State already contains an action: ${ACTION_ID}"
    wait_for_api
    ARTIFACT_SHA256="$(sha256sum "${artifact}" | awk '{print $1}')"

    module_response="$(json_module "${TARGET_ID}" | \
        curl_api --request POST --header 'Content-Type: application/json' --data-binary @- \
        "${MANAGEMENT_URL}/rest/v1/softwaremodules")"
    module_id="$(printf '%s' "${module_response}" | first_id)"

    artifact_response="$(curl_api --request POST \
        --form "file=@${artifact};type=application/octet-stream" \
        "${MANAGEMENT_URL}/rest/v1/softwaremodules/${module_id}/artifacts")"
    printf '%s' "${artifact_response}" | verify_artifact "$(stat -c '%s' "${artifact}")" \
        "$(sha1sum "${artifact}" | awk '{print $1}')" "${ARTIFACT_SHA256}"

    distribution_response="$(json_distribution_set "${TARGET_ID}" "${module_id}" | \
        curl_api --request POST --header 'Content-Type: application/json' --data-binary @- \
        "${MANAGEMENT_URL}/rest/v1/distributionsets")"
    distribution_id="$(printf '%s' "${distribution_response}" | first_id)"
    assignment_response="$(json_assignment "${distribution_id}" | \
        curl_api --request POST --header 'Content-Type: application/json' --data-binary @- \
        "${MANAGEMENT_URL}/rest/v1/targets/${TARGET_ID}/assignedDS")"
    : "${assignment_response}"

    for _ in $(seq 1 10); do
        if ACTION_ID="$(curl_api "${MANAGEMENT_URL}/rest/v1/targets/${TARGET_ID}/actions" | latest_action_id)"; then
            write_state "${state_file}"
            printf 'STATE_FILE=%s\nTARGET_ID=%s\nACTION_ID=%s\nARTIFACT_SHA256=%s\n' \
                "${state_file}" "${TARGET_ID}" "${ACTION_ID}" "${ARTIFACT_SHA256}"
            return 0
        fi
        sleep 1
    done
    fail "hawkBit action was not created"
}

main()
{
    local command="${1:-}"
    local artifact=""
    local hawkbit_host=""
    local hawkbit_config=""
    local target_address=""
    local state_file="${STATE_DIRECTORY}/action.env"

    test $# -gt 0 || { usage; exit 2; }
    shift
    while test $# -gt 0; do
        case "$1" in
            --artifact) artifact="${2:?Missing artifact path}"; shift 2 ;;
            --hawkbit-host) hawkbit_host="${2:?Missing hawkBit host}"; shift 2 ;;
            --hawkbit-config) hawkbit_config="${2:?Missing hawkBit config path}"; shift 2 ;;
            --target-address) target_address="${2:?Missing target address}"; shift 2 ;;
            --state) state_file="${2:?Missing state path}"; shift 2 ;;
            -h|--help) usage; exit 0 ;;
            *) fail "Unknown argument: $1" ;;
        esac
    done

    case "${command}" in
        provision)
            test -z "${artifact}" && test -z "${hawkbit_host}" && -n "${hawkbit_config}" &&
                test -n "${target_address}" || {
                usage
                exit 2
            }
            provision_target "${hawkbit_config}" "${target_address}" "${state_file}"
            ;;
        register)
            test -z "${artifact}" && test -z "${hawkbit_config}" && test -n "${hawkbit_host}" && test -n "${target_address}" || {
                usage
                exit 2
            }
            load_credentials
            register_target "${hawkbit_host}" "${target_address}" "${state_file}"
            ;;
        assign)
            test -n "${artifact}" && test -z "${hawkbit_host}" && test -z "${hawkbit_config}" && test -z "${target_address}" || {
                usage
                exit 2
            }
            assign_action "${artifact}" "${state_file}"
            ;;
        status)
            load_credentials
            load_state "${state_file}"
            print_status
            ;;
        *)
            usage
            exit 2
            ;;
    esac
}

main "$@"
