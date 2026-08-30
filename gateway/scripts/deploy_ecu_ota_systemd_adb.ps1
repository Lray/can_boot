[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [string]$ArtifactDirectory = 'E:\T527\can_boot\gateway\build-target-hawkbit-e2e',
    [Parameter(Mandatory = $true)][string]$HawkBitConfig,
    [Parameter(Mandatory = $true)][string]$WpaSupplicantConfig,
    [Parameter(Mandatory = $true)][string]$ImagePath,
    [Parameter(Mandatory = $true)][string]$TrustKeyPath,
    [string]$RemoteRoot = '/run/media/mmcblk0p6/ecu-ota',
    [string]$WifiInterface = 'wlan0',
    [string]$CanInterface = 'awlink0',
    [string]$JobId = ([guid]::NewGuid().ToString()),
    [ValidateRange(1, 65535)][int]$SignerUid = 200,
    [ValidateRange(1, 65535)][int]$SignerGid = 200,
    [ValidateRange(1, 65535)][int]$SignerSocketGid = 201,
    [ValidateRange(1, 1000)][int]$SignerTimeoutMs = 1000,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ArtifactNames = @('ecu-ota-orchestrator', 'gateway-ota-worker-v1', 'token-signer-daemon')
$ConfigRoot = Join-Path $PSScriptRoot '..\config'
$UnitRoot = Join-Path $ConfigRoot 'systemd'
$RuntimeScripts = @('swupdate-suricatta', 'wait-for-remote-handler', 'wait-for-token-signer')

function Assert-LocalRegularFile {
    param([string]$Label, [string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label does not exist: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.LinkType) { throw "$Label must not be a symbolic link: $Path" }
    $item.FullName
}

function Get-HawkBitSettings {
    param([string]$Path)

    $settings = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) { continue }
        if ($trimmed -notmatch '^([A-Z0-9_]+)=([^\s#]+)$') {
            throw 'hawkbit.conf contains an unsupported line format.'
        }
        if ($settings.ContainsKey($Matches[1])) {
            throw "hawkbit.conf repeats $($Matches[1])."
        }
        $settings[$Matches[1]] = $Matches[2]
    }
    foreach ($name in @(
        'HAWKBIT_SERVER_URL', 'HAWKBIT_TENANT', 'HAWKBIT_TARGET_ID',
        'HAWKBIT_TARGET_TOKEN', 'SWUPDATE_TRUST_KEY', 'SWUPDATE_LOG_LEVEL',
        'SWUPDATE_POLL_DELAY', 'SWUPDATE_RETRIES', 'SWUPDATE_RETRY_WAIT')) {
        if (-not $settings.ContainsKey($name)) { throw "hawkbit.conf is missing $name." }
    }
    if ($settings.HAWKBIT_SERVER_URL -notmatch '^https?://[A-Za-z0-9.-]+:[0-9]{1,5}$') {
        throw 'HAWKBIT_SERVER_URL must be an explicit http(s) host:port address.'
    }
    $uri = [uri]$settings.HAWKBIT_SERVER_URL
    if ($uri.Host -in @('127.0.0.1', 'localhost', '::1')) {
        throw 'HAWKBIT_SERVER_URL must be the board-reachable fixed LAN address, not loopback.'
    }
    if ($settings.HAWKBIT_TENANT -notmatch '^[A-Za-z0-9._-]+$' -or
        $settings.HAWKBIT_TARGET_ID -notmatch '^[A-Za-z0-9._-]+$' -or
        $settings.HAWKBIT_TARGET_TOKEN -notmatch '^[0-9a-f]{32}$') {
        throw 'hawkbit.conf tenant, target ID, or target token is malformed.'
    }
    foreach ($name in @('SWUPDATE_LOG_LEVEL', 'SWUPDATE_POLL_DELAY', 'SWUPDATE_RETRIES', 'SWUPDATE_RETRY_WAIT')) {
        if ($settings[$name] -notmatch '^[0-9]+$') { throw "hawkbit.conf has invalid $name." }
    }
    $settings
}

function Invoke-AdbResult {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $AdbPath -s $Serial @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previous }
    [pscustomobject]@{ ExitCode = $exitCode; Output = @($output) }
}

function Invoke-Adb {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $result = Invoke-AdbResult @Arguments
    if ($result.ExitCode -ne 0) {
        throw "adb failed ($($result.ExitCode)): $($Arguments -join ' ')`n$($result.Output -join "`n")"
    }
    $result.Output
}

function Invoke-AdbShell {
    param([Parameter(Mandatory = $true)][string]$Command)

    Invoke-Adb shell $Command | Out-Null
}

function Test-BoardTcpReachability {
    param([string]$HostName, [int]$Port)

    $command = "command -v nc >/dev/null 2>&1; nc -z -w 5 '$HostName' '$Port'"
    $result = Invoke-AdbResult shell $command
    if ($result.ExitCode -ne 0) {
        throw "Board cannot open TCP $HostName`:$Port over its current Wi-Fi route. Do not start SWUpdate."
    }
}

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required.' }
if ($Serial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'Device serial contains unsupported characters.' }
if ($RemoteRoot -notmatch '^/[A-Za-z0-9._/-]+$' -or $RemoteRoot.Contains('..') -or $RemoteRoot.Contains('//')) {
    throw 'RemoteRoot is not a safe absolute path.'
}
if ($WifiInterface -notmatch '^[A-Za-z0-9_.-]{1,15}$' -or $CanInterface -notmatch '^[A-Za-z0-9_.-]{1,15}$') {
    throw 'Network interface name is invalid.'
}
if ($WifiInterface -ne 'wlan0') {
    throw 'The supplied systemd network unit is intentionally bound to wlan0.'
}
if ($JobId -notmatch '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$') {
    throw 'JobId must be a UUID v4.'
}

$HawkBitConfig = Assert-LocalRegularFile 'hawkbit.conf' $HawkBitConfig
$WpaSupplicantConfig = Assert-LocalRegularFile 'wpa_supplicant.conf' $WpaSupplicantConfig
$ImagePath = Assert-LocalRegularFile 'release image' $ImagePath
$TrustKeyPath = Assert-LocalRegularFile 'SWU trust key' $TrustKeyPath
$settings = Get-HawkBitSettings $HawkBitConfig
$hawkBitUri = [uri]$settings.HAWKBIT_SERVER_URL
if ($settings.SWUPDATE_TRUST_KEY -ne '/etc/ecu-ota/trust/swu-release.pem') {
    throw 'hawkbit.conf SWUPDATE_TRUST_KEY must be /etc/ecu-ota/trust/swu-release.pem for this deployment.'
}

$imageItem = Get-Item -LiteralPath $ImagePath
if ($imageItem.Length -le 0 -or $imageItem.Length -gt 131072) { throw 'Release image size is outside the ECU slot bound.' }
$imageHash = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash.ToLowerInvariant()
$artifacts = foreach ($name in $ArtifactNames) {
    $path = Assert-LocalRegularFile "artifact $name" (Join-Path $ArtifactDirectory $name)
    [pscustomobject]@{ Name = $name; Path = $path; Sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
foreach ($name in $RuntimeScripts) { Assert-LocalRegularFile "runtime script $name" (Join-Path $PSScriptRoot $name) | Out-Null }
foreach ($name in @(
    'ecu-ota-wifi.service', 'ecu-ota-wpa-supplicant.service', 'ecu-ota-network-online.service',
    'ecu-ota-token-signer.service', 'ecu-ota-orchestrator.service', 'ecu-ota-swupdate.service',
    '80-ecu-ota-wlan0.network', 'wifi.conf.example', 'signer.conf.example', 'orchestrator.conf.example')) {
    Assert-LocalRegularFile "systemd source $name" (Join-Path $UnitRoot $name) | Out-Null
}

if ($ValidateOnly) {
    Write-Output 'Local systemd deployment inputs are valid.'
    Write-Output "Release image bytes=$($imageItem.Length) sha256=$imageHash job=$JobId"
    return
}
if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) { throw "adb executable does not exist: $AdbPath" }
if (((Invoke-Adb get-state | Select-Object -Last 1).Trim()) -ne 'device') { throw "ADB device is not ready: $Serial" }

# Fail before writing anything when this is the current BusyBox/Android HIL rootfs.
Invoke-AdbShell 'test "$(cat /proc/1/comm)" = systemd'
Invoke-AdbShell 'command -v systemctl >/dev/null && command -v install >/dev/null && command -v nc >/dev/null && test -x /usr/sbin/ip && test -x /usr/sbin/wpa_supplicant && test -x /usr/lib/systemd/systemd-networkd-wait-online'
Invoke-AdbShell 'systemctl cat tee-supplicant.service >/dev/null'
Invoke-AdbShell "test -x /sbin/swupdate && test -d '$RemoteRoot' && test ! -L '$RemoteRoot'"

$localStage = Join-Path ([IO.Path]::GetTempPath()) ('ecu-ota-systemd-' + [guid]::NewGuid().ToString('N'))
$remoteStage = '/data/local/tmp/ecu-ota-systemd-' + [guid]::NewGuid().ToString('N')
try {
    New-Item -ItemType Directory -LiteralPath $localStage | Out-Null
    $orchestratorConfig = @(
        "ECU_OTA_JOB_ID=$JobId",
        "ECU_OTA_EXPECTED_SIZE=$($imageItem.Length)",
        "ECU_OTA_EXPECTED_SHA256=$imageHash",
        'ECU_OTA_WORK_ROOT=/var/lib/ecu-ota/jobs',
        "ECU_OTA_CAN_IFNAME=$CanInterface",
        'ECU_OTA_REMOTE_ENDPOINT=ipc:///run/ecu-ota/remote-handler/ecu-v1',
        'ECU_TOKEN_SIGNER_ENDPOINT=/run/ecu-token-signer/v1.sock',
        "ECU_TOKEN_SIGNER_UID=$SignerUid",
        "ECU_TOKEN_SIGNER_GID=$SignerGid",
        "ECU_TOKEN_SIGNER_SOCKET_GID=$SignerSocketGid",
        "ECU_TOKEN_SIGNER_TIMEOUT_MS=$SignerTimeoutMs"
    ) -join "`n"
    $signerConfig = @(
        'ECU_TOKEN_SIGNER_ENDPOINT=/run/ecu-token-signer/v1.sock',
        "ECU_TOKEN_SIGNER_UID=$SignerUid",
        "ECU_TOKEN_SIGNER_GID=$SignerGid",
        "ECU_TOKEN_SIGNER_SOCKET_GID=$SignerSocketGid",
        'ECU_TOKEN_SIGNER_CLIENT_UID=0',
        'ECU_TOKEN_SIGNER_CLIENT_GID=0'
    ) -join "`n"
    Set-Content -LiteralPath (Join-Path $localStage 'orchestrator.conf') -Value ($orchestratorConfig + "`n") -Encoding ascii -NoNewline
    Set-Content -LiteralPath (Join-Path $localStage 'signer.conf') -Value ($signerConfig + "`n") -Encoding ascii -NoNewline
    Set-Content -LiteralPath (Join-Path $localStage 'wifi.conf') -Value ("ECU_WIFI_IFNAME=$WifiInterface`n") -Encoding ascii -NoNewline

    Invoke-AdbShell "umask 077; mkdir '$remoteStage'; mkdir '$remoteStage/bin' '$remoteStage/libexec' '$remoteStage/systemd' '$remoteStage/config' '$remoteStage/trust'"
    foreach ($artifact in $artifacts) { Invoke-Adb push $artifact.Path "$remoteStage/bin/$($artifact.Name)" | Out-Null }
    foreach ($name in $RuntimeScripts) { Invoke-Adb push (Join-Path $PSScriptRoot $name) "$remoteStage/libexec/$name" | Out-Null }
    foreach ($name in @('ecu-ota-wifi.service', 'ecu-ota-wpa-supplicant.service', 'ecu-ota-network-online.service', 'ecu-ota-token-signer.service', 'ecu-ota-orchestrator.service', 'ecu-ota-swupdate.service', '80-ecu-ota-wlan0.network')) {
        Invoke-Adb push (Join-Path $UnitRoot $name) "$remoteStage/systemd/$name" | Out-Null
    }
    Invoke-Adb push $HawkBitConfig "$remoteStage/config/hawkbit.conf" | Out-Null
    Invoke-Adb push $WpaSupplicantConfig "$remoteStage/config/wpa_supplicant.conf" | Out-Null
    Invoke-Adb push (Join-Path $localStage 'orchestrator.conf') "$remoteStage/config/orchestrator.conf" | Out-Null
    Invoke-Adb push (Join-Path $localStage 'signer.conf') "$remoteStage/config/signer.conf" | Out-Null
    Invoke-Adb push (Join-Path $localStage 'wifi.conf') "$remoteStage/config/wifi.conf" | Out-Null
    Invoke-Adb push $TrustKeyPath "$remoteStage/trust/swu-release.pem" | Out-Null

    foreach ($artifact in $artifacts) {
        $remoteHash = ((Invoke-Adb shell "sha256sum '$remoteStage/bin/$($artifact.Name)'") -join '').Split(' ')[0]
        if ($remoteHash -ne $artifact.Sha256) { throw "Staged artifact hash mismatch: $($artifact.Name)" }
    }

    Invoke-AdbShell 'systemctl stop ecu-ota-swupdate.service ecu-ota-orchestrator.service 2>/dev/null || true'
    Invoke-AdbShell "install -d -o root -g root -m 0750 '$RemoteRoot/bin' /usr/libexec/ecu-ota /etc/ecu-ota /etc/ecu-ota/trust /etc/systemd/system /etc/systemd/network /var/lib/ecu-ota/jobs"
    Invoke-AdbShell "install -o root -g root -m 0755 '$remoteStage/bin/ecu-ota-orchestrator' '$RemoteRoot/bin/ecu-ota-orchestrator'; install -o root -g root -m 0755 '$remoteStage/bin/gateway-ota-worker-v1' '$RemoteRoot/bin/gateway-ota-worker-v1'; install -o root -g root -m 0755 '$remoteStage/bin/token-signer-daemon' '$RemoteRoot/bin/token-signer-daemon'"
    Invoke-AdbShell "install -o root -g root -m 0755 '$remoteStage/libexec/swupdate-suricatta' /usr/libexec/ecu-ota/swupdate-suricatta; install -o root -g root -m 0755 '$remoteStage/libexec/wait-for-remote-handler' /usr/libexec/ecu-ota/wait-for-remote-handler; install -o root -g root -m 0755 '$remoteStage/libexec/wait-for-token-signer' /usr/libexec/ecu-ota/wait-for-token-signer"
    Invoke-AdbShell "install -o root -g root -m 0644 '$remoteStage/systemd/ecu-ota-wifi.service' /etc/systemd/system/ecu-ota-wifi.service; install -o root -g root -m 0644 '$remoteStage/systemd/ecu-ota-wpa-supplicant.service' /etc/systemd/system/ecu-ota-wpa-supplicant.service; install -o root -g root -m 0644 '$remoteStage/systemd/ecu-ota-network-online.service' /etc/systemd/system/ecu-ota-network-online.service; install -o root -g root -m 0644 '$remoteStage/systemd/ecu-ota-token-signer.service' /etc/systemd/system/ecu-ota-token-signer.service; install -o root -g root -m 0644 '$remoteStage/systemd/ecu-ota-orchestrator.service' /etc/systemd/system/ecu-ota-orchestrator.service; install -o root -g root -m 0644 '$remoteStage/systemd/ecu-ota-swupdate.service' /etc/systemd/system/ecu-ota-swupdate.service; install -o root -g root -m 0644 '$remoteStage/systemd/80-ecu-ota-wlan0.network' /etc/systemd/network/80-ecu-ota-wlan0.network"
    Invoke-AdbShell "install -o root -g root -m 0600 '$remoteStage/config/hawkbit.conf' /etc/ecu-ota/hawkbit.conf; install -o root -g root -m 0600 '$remoteStage/config/wpa_supplicant.conf' /etc/ecu-ota/wpa_supplicant.conf; install -o root -g root -m 0640 '$remoteStage/config/orchestrator.conf' /etc/ecu-ota/orchestrator.conf; install -o root -g root -m 0640 '$remoteStage/config/signer.conf' /etc/ecu-ota/signer.conf; install -o root -g root -m 0640 '$remoteStage/config/wifi.conf' /etc/ecu-ota/wifi.conf; install -o root -g root -m 0400 '$remoteStage/trust/swu-release.pem' /etc/ecu-ota/trust/swu-release.pem"
    Invoke-AdbShell "rm -rf '$remoteStage'; systemctl daemon-reload; systemctl enable systemd-networkd.service ecu-ota-wifi.service ecu-ota-wpa-supplicant.service ecu-ota-token-signer.service ecu-ota-orchestrator.service ecu-ota-swupdate.service"
    Invoke-AdbShell 'systemctl restart systemd-networkd.service ecu-ota-wifi.service ecu-ota-wpa-supplicant.service ecu-ota-network-online.service'
    Test-BoardTcpReachability -HostName $hawkBitUri.Host -Port $hawkBitUri.Port
    Invoke-AdbShell 'systemctl restart ecu-ota-token-signer.service ecu-ota-orchestrator.service ecu-ota-swupdate.service'
    Invoke-AdbShell 'systemctl is-active --quiet ecu-ota-wpa-supplicant.service ecu-ota-token-signer.service ecu-ota-orchestrator.service ecu-ota-swupdate.service'
}
finally {
    if (Test-Path -LiteralPath $localStage) { Remove-Item -LiteralPath $localStage -Recurse -Force }
    if ($remoteStage) { Invoke-AdbResult shell "rm -rf '$remoteStage'" | Out-Null }
}

Write-Output 'ECU OTA systemd runtime is deployed and active.'
Write-Output "Release binding installed: job=$JobId bytes=$($imageItem.Length) sha256=$imageHash"
