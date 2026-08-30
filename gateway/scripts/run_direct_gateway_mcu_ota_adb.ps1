[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [string]$ImagePath = 'E:\T527\can_boot\can\build\gateway-ota-slot1-v102-sec17-4eb0bba\image.bin',
    [string]$RemoteRoot = '/run/media/mmcblk0p6/ecu-ota',
    [string]$CanConfigPath = (Join-Path $PSScriptRoot '..\config\systemd\awlink0.conf'),
    [ValidateRange(1, 65535)][int]$SignerUid = 200,
    [ValidateRange(1, 65535)][int]$SignerGid = 200,
    [ValidateRange(1, 65535)][int]$SignerSocketGid = 201,
    [ValidateRange(1, 60000)][int]$SignerTimeoutMs = 1000,
    [string]$JobId = ([guid]::NewGuid().ToString())
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-SafeAbsolutePath {
    param([string]$Label, [string]$Path)

    if ($Path -notmatch '^/[A-Za-z0-9._/-]+$' -or $Path.Contains('..') -or $Path.Contains('//')) {
        throw "$Label is not a safe absolute POSIX path."
    }
}

function Assert-LocalRegularFile {
    param([string]$Label, [string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label does not exist: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.LinkType) {
        throw "$Label must not be a symbolic link: $Path"
    }
    return $item
}

function Get-CanSettings {
    param([string]$Path)

    $Path = Assert-LocalRegularFile 'awlink0.conf' $Path
    $settings = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) { continue }
        if ($trimmed -notmatch '^([A-Z0-9_]+)=([^\s#]+)$') {
            throw 'awlink0.conf contains an unsupported line format.'
        }
        if ($settings.ContainsKey($Matches[1])) {
            throw "awlink0.conf repeats $($Matches[1])."
        }
        $settings[$Matches[1]] = $Matches[2]
    }
    if (($settings.Keys | Sort-Object) -join ',' -ne 'ECU_OTA_CAN_BITRATE,ECU_OTA_CAN_IFNAME,ECU_OTA_CAN_MTU') {
        throw 'awlink0.conf must define exactly ECU_OTA_CAN_IFNAME, ECU_OTA_CAN_BITRATE, and ECU_OTA_CAN_MTU.'
    }
    if ($settings.ECU_OTA_CAN_IFNAME -notmatch '^[A-Za-z0-9_.-]{1,15}$') {
        throw 'awlink0.conf has an invalid ECU_OTA_CAN_IFNAME.'
    }
    if ($settings.ECU_OTA_CAN_BITRATE -notmatch '^[0-9]+$' -or [int]$settings.ECU_OTA_CAN_BITRATE -ne 500000) {
        throw 'awlink0.conf ECU_OTA_CAN_BITRATE must be 500000.'
    }
    if ($settings.ECU_OTA_CAN_MTU -ne '16') {
        throw 'awlink0.conf ECU_OTA_CAN_MTU must be 16 for Classic CAN.'
    }
    return $settings
}

function Invoke-AdbResult {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $AdbPath -s $Serial @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    [pscustomobject]@{ ExitCode = $exitCode; Output = @($output) }
}

function Invoke-Adb {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $result = Invoke-AdbResult @Arguments
    if ($result.ExitCode -ne 0) {
        throw "adb failed ($($result.ExitCode)): $($Arguments -join ' ')`n$($result.Output -join "`n")"
    }
    return $result.Output
}

if ($PSVersionTable.PSVersion.Major -lt 5) {
    throw 'PowerShell 5 or later is required.'
}
if ($Serial -notmatch '^[A-Za-z0-9._:-]+$') {
    throw 'Device serial contains unsupported characters.'
}
Assert-SafeAbsolutePath 'RemoteRoot' $RemoteRoot
$canSettings = Get-CanSettings $CanConfigPath
$CanInterface = $canSettings.ECU_OTA_CAN_IFNAME
$CanBitrate = [int]$canSettings.ECU_OTA_CAN_BITRATE
$CanMtu = [int]$canSettings.ECU_OTA_CAN_MTU
if ($JobId -notmatch '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$') {
    throw 'JobId must be a UUID v4.'
}
if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) {
    throw "adb executable does not exist: $AdbPath"
}

$image = Assert-LocalRegularFile 'MCU image' $ImagePath
if ($image.Length -ne 131072) {
    throw "MCU image must occupy one complete 131072-byte slot; got $($image.Length) bytes."
}
$imageHash = (Get-FileHash -LiteralPath $image.FullName -Algorithm SHA256).Hash.ToLowerInvariant()

if (((Invoke-Adb get-state | Select-Object -Last 1).Trim()) -ne 'device') {
    throw "ADB device is not ready: $Serial"
}

$remoteBin = "$RemoteRoot/bin"
$worker = "$remoteBin/gateway-ota-worker-v1"
$signer = "$remoteBin/token-signer-daemon"
$signerEndpoint = '/run/ecu-token-signer/v1.sock'
$jobRoot = "$RemoteRoot/var/direct-jobs"
$jobDirectory = "$jobRoot/$JobId"
$inputDirectory = "$jobDirectory/package-input-v1"
$remoteImage = "$inputDirectory/image.bin"
$workerLog = "$jobDirectory/worker.log"
$signerLog = "$jobDirectory/signer.log"
$signerPid = "$jobDirectory/signer.pid"
$teeSupplicantLog = "$jobDirectory/tee-supplicant.log"
$teeSupplicantPid = "$jobDirectory/tee-supplicant.pid"

try {
    # This is intentionally a direct worker invocation.  It refuses the remote handler path.
    Invoke-Adb shell (
        "set -eu; " +
        "test `"`$(id -u)`" = 0; " +
        "test -d '$RemoteRoot' && test ! -L '$RemoteRoot'; " +
        "test -d '$remoteBin' && test ! -L '$remoteBin'; " +
        "test -f '$worker' && test ! -L '$worker' && test -x '$worker'; " +
        "test -f '$signer' && test ! -L '$signer' && test -x '$signer'; " +
        "test -c /dev/tee0 && test -r /usr/lib64/libteec.so.1 && test -r /usr/lib64/libcrypto.so.1.1; " +
        "test -f /lib/optee_armtz/724b12aa-6e74-4779-bf3a-1580a076fed3.ta; " +
        "test -x /usr/sbin/tee-supplicant; " +
        "ip link show '$CanInterface' >/dev/null; " +
        "ip link set '$CanInterface' down; " +
"ip link set '$CanInterface' type can bitrate $CanBitrate; " +
"ip link set '$CanInterface' up; " +
"ip link set '$CanInterface' mtu $CanMtu; " +
"ip link show '$CanInterface' | grep -q 'mtu $CanMtu'; " +
"ip -details link show '$CanInterface' | grep -q 'bitrate $CanBitrate'; " +
"ip -details link show '$CanInterface' | grep -q 'state ERROR-ACTIVE'; " +
        "! start-stop-daemon -K -t -x /sbin/swupdate >/dev/null 2>&1; " +
        "! start-stop-daemon -K -t -x '$remoteBin/ecu-ota-orchestrator' >/dev/null 2>&1; " +
        "! start-stop-daemon -K -t -x '$signer' >/dev/null 2>&1; " +
        "if [ -e '$jobRoot' ]; then test -d '$jobRoot' && test ! -L '$jobRoot'; else mkdir -p '$jobRoot'; fi; " +
        "chown root:root '$jobRoot'; chmod 0700 '$jobRoot'; " +
        "mkdir '$jobDirectory'; mkdir '$inputDirectory'; " +
        "chown root:root '$jobDirectory' '$inputDirectory'; chmod 0700 '$jobDirectory' '$inputDirectory'"
    ) | Out-Null

    Invoke-Adb push $image.FullName $remoteImage | Out-Null
    Invoke-Adb shell "set -eu; chown root:root '$remoteImage'; chmod 0400 '$remoteImage'; sync" | Out-Null
    $remoteHash = ((Invoke-Adb shell "sha256sum '$remoteImage'") -join "`n").Split(' ')[0]
    if ($remoteHash -ne $imageHash) {
        throw 'Remote image hash mismatch; the worker was not started.'
    }

    $expectedSocketPolicy = "$SignerUid`:$SignerSocketGid`:660:1"
    # ADB reaps background children at the end of a shell call on this image.
    # Keep signer and worker in the same shell so the socket survives the OTA.
    $directCommand = @'
set -eu
if [ -e '__SIGNER_ENDPOINT__' ]; then
    test -S '__SIGNER_ENDPOINT__'
    rm -f '__SIGNER_ENDPOINT__'
fi
if [ -e /run/ecu-token-signer ]; then
    test -d /run/ecu-token-signer && test ! -L /run/ecu-token-signer
fi
umask 077
started_tee_supplicant=0
tee_supplicant_pid=''
if ! pidof tee-supplicant >/dev/null 2>&1; then
    /usr/sbin/tee-supplicant > '__TEE_SUPPLICANT_LOG__' 2>&1 &
    tee_supplicant_pid=$!
    started_tee_supplicant=1
    printf '%s\n' "$tee_supplicant_pid" > '__TEE_SUPPLICANT_PID__'
    sleep 1
fi
'__SIGNER__' --endpoint '__SIGNER_ENDPOINT__' --uid __SIGNER_UID__ --gid __SIGNER_GID__ \
    --socket-gid __SOCKET_GID__ --client-uid 0 --client-gid 0 --idle-timeout 600 \
    > '__SIGNER_LOG__' 2>&1 &
signer_pid=$!
printf '%s\n' "$signer_pid" > '__SIGNER_PID__'
socket_ready=0
attempt=0
while [ "$attempt" -lt 10 ]; do
    if [ -S '__SIGNER_ENDPOINT__' ]; then
        socket_policy=$(stat -c '%u:%g:%a:%h' '__SIGNER_ENDPOINT__')
        if [ "$socket_policy" = '__EXPECTED_SOCKET_POLICY__' ]; then
            socket_ready=1
        else
            printf '%s\n' "token-signer socket policy mismatch: expected=__EXPECTED_SOCKET_POLICY__ actual=$socket_policy" >&2
        fi
        break
    fi
    attempt=$((attempt + 1))
    sleep 1
done
if [ "$socket_ready" -ne 1 ]; then
    cat '__TEE_SUPPLICANT_LOG__' >&2 || true
    cat '__SIGNER_LOG__' >&2 || true
    kill "$signer_pid" 2>/dev/null || true
    if [ "$started_tee_supplicant" -eq 1 ]; then kill "$tee_supplicant_pid" 2>/dev/null || true; fi
    rm -f '__SIGNER_ENDPOINT__'
    exit 70
fi
set +e
'__WORKER__' --job-dir '__JOB_DIR__' --job-id '__JOB_ID__' --ifname '__CAN_IFNAME__' \
    --signer-endpoint '__SIGNER_ENDPOINT__' --signer-uid __SIGNER_UID__ --signer-gid __SIGNER_GID__ \
    --signer-socket-gid __SOCKET_GID__ --signer-timeout-ms __SIGNER_TIMEOUT_MS__ \
    > '__WORKER_LOG__' 2>&1
worker_status=$?
set -e
cat '__WORKER_LOG__'
kill "$signer_pid" 2>/dev/null || true
if [ "$started_tee_supplicant" -eq 1 ]; then kill "$tee_supplicant_pid" 2>/dev/null || true; fi
rm -f '__SIGNER_ENDPOINT__'
exit "$worker_status"
'@
    $directCommand = $directCommand.Replace('__SIGNER__', $signer).
        Replace('__SIGNER_ENDPOINT__', $signerEndpoint).
        Replace('__SIGNER_UID__', [string]$SignerUid).
        Replace('__SIGNER_GID__', [string]$SignerGid).
        Replace('__SOCKET_GID__', [string]$SignerSocketGid).
        Replace('__SIGNER_LOG__', $signerLog).
        Replace('__SIGNER_PID__', $signerPid).
        Replace('__TEE_SUPPLICANT_LOG__', $teeSupplicantLog).
        Replace('__TEE_SUPPLICANT_PID__', $teeSupplicantPid).
        Replace('__EXPECTED_SOCKET_POLICY__', $expectedSocketPolicy).
        Replace('__WORKER__', $worker).
        Replace('__JOB_DIR__', $jobDirectory).
        Replace('__JOB_ID__', $JobId).
        Replace('__CAN_IFNAME__', $CanInterface).
        Replace('__SIGNER_TIMEOUT_MS__', [string]$SignerTimeoutMs).
        Replace('__WORKER_LOG__', $workerLog)
    $workerResult = Invoke-AdbResult shell $directCommand
    $workerOutput = $workerResult.Output -join "`n"
    if ($workerResult.ExitCode -ne 0 -or $workerOutput -notmatch 'CONFIRMED') {
        throw "Gateway-to-MCU OTA did not confirm. Job log retained at $workerLog.`n$workerOutput"
    }

    Invoke-Adb shell (
        "! start-stop-daemon -K -t -x /sbin/swupdate >/dev/null 2>&1; " +
        "! start-stop-daemon -K -t -x '$remoteBin/ecu-ota-orchestrator' >/dev/null 2>&1"
    ) | Out-Null
    Write-Output 'Gateway-to-MCU direct OTA confirmed.'
    Write-Output "job=$JobId image_sha256=$imageHash"
    Write-Output "board_logs=$jobDirectory"
}
finally {
    Invoke-AdbResult shell (
        "if [ -f '$signerPid' ]; then kill `$(cat '$signerPid') 2>/dev/null || true; fi; " +
        "rm -f '$signerEndpoint'"
    ) | Out-Null
}
