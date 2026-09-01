#!/bin/sh
# Ephemeral BusyBox HIL runtime.  The caller installs this under /run only.
# Production startup remains the systemd unit set in config/systemd/.
set -eu

runtime=/run/mcu-update/hawkbit-test
bin=/run/media/mmcblk0p6/mcu-update/bin
signer_socket=/run/mcu-token-signer/v1.sock
remote_name="$(strings /sbin/swupdate | sed -n '/^[a-z][a-z]*-v1$/ { p; q; }')"
[ -n "$remote_name" ] || { echo 'SWUpdate remote-handler service name is unavailable' >&2; exit 1; }
remote_directory="$(strings /sbin/swupdate | sed -n '\#^/run/.*/remote-handler/$# { p; q; }')"
[ -n "$remote_directory" ] || { echo 'SWUpdate remote-handler IPC directory is unavailable' >&2; exit 1; }
remote_socket="${remote_directory}${remote_name}"
remote_endpoint="ipc://${remote_socket}"

mkdir -p "$runtime/log" /run/mcu-token-signer "$remote_directory" /run/mcu-update/jobs
rm -f "$signer_socket" "$remote_socket"

start-stop-daemon -K -o -x "$bin/mcu-updater" >/dev/null 2>&1 || true
start-stop-daemon -K -o -x "$bin/token-signer-daemon" >/dev/null 2>&1 || true

if ! pidof tee-supplicant >/dev/null 2>&1; then
    start-stop-daemon -S -b -x /usr/sbin/tee-supplicant \
        >"$runtime/log/tee-supplicant.log" 2>&1
fi

"$bin/token-signer-daemon" \
    --endpoint "$signer_socket" --uid 200 --gid 200 --socket-gid 201 \
    --client-uid 0 --client-gid 0 --idle-timeout 600 \
    >"$runtime/log/signer.log" 2>&1 &
signer_pid=$!
printf '%s\n' "$signer_pid" >"$runtime/signer.pid"

attempt=0
while [ "$attempt" -lt 10 ]; do
    [ -S "$signer_socket" ] && break
    attempt=$((attempt + 1))
    sleep 1
done
[ -S "$signer_socket" ] || { echo 'token signer did not become ready' >&2; exit 1; }

"$bin/mcu-updater" \
    --work-root /run/mcu-update/jobs --ifname awlink0 \
    --endpoint "$remote_endpoint" --signer-endpoint "$signer_socket" \
    --signer-uid 200 --signer-gid 200 --signer-socket-gid 201 \
    --signer-timeout-ms 1000 \
    >"$runtime/log/mcu-updater.log" 2>&1 &
updater_pid=$!
printf '%s\n' "$updater_pid" >"$runtime/updater.pid"

attempt=0
while [ "$attempt" -lt 30 ]; do
    [ -S "$remote_socket" ] && break
    attempt=$((attempt + 1))
    sleep 1
done
[ -S "$remote_socket" ] || { echo 'remote handler did not become ready' >&2; exit 1; }

. "$runtime/hawkbit.conf"
export HAWKBIT_SERVER_URL HAWKBIT_TENANT HAWKBIT_TARGET_ID HAWKBIT_TARGET_TOKEN
export SWUPDATE_TRUST_KEY SWUPDATE_LOG_LEVEL SWUPDATE_POLL_DELAY SWUPDATE_RETRIES
export SWUPDATE_RETRY_WAIT
set +e
"$runtime/swupdate-suricatta" >"$runtime/log/swupdate.log" 2>&1
status=$?
set -e
kill "$updater_pid" "$signer_pid" 2>/dev/null || true
wait "$updater_pid" "$signer_pid" 2>/dev/null || true
rm -f "$signer_socket" "$remote_socket"
cat "$runtime/log/swupdate.log"
exit "$status"
