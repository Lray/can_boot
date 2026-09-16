[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [string]$ArtifactDirectory = 'E:\T527\can_boot\gateway\build-target-hawkbit-e2e',
    [Parameter(Mandatory = $true)][string]$HawkBitConfig,
    [Parameter(Mandatory = $true)][string]$WpaSupplicantConfig,
    [Parameter(Mandatory = $true)][string]$TrustKeyPath,
    [string]$RemoteRoot = '/run/media/mmcblk0p6/mcu-update',
    [string]$WifiInterface = 'wlan0',
    [string]$CanInterface = 'awlink0',
    [ValidateRange(1, 65535)][int]$SignerUid = 200,
    [ValidateRange(1, 65535)][int]$SignerGid = 200,
    [ValidateRange(1, 65535)][int]$SignerSocketGid = 201,
    [ValidateRange(1, 1000)][int]$SignerTimeoutMs = 1000,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ArtifactNames = @('mcu-updater', 'token-signer-daemon', 'gateway-lss-master')
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
if ($RemoteRoot -ne '/run/media/mmcblk0p6/mcu-update') {
    throw 'RemoteRoot must match the fixed paths in the production systemd units.'
}
if ($WifiInterface -notmatch '^[A-Za-z0-9_.-]{1,15}$' -or $CanInterface -notmatch '^[A-Za-z0-9_.-]{1,15}$') {
    throw 'Network interface name is invalid.'
}
if ($WifiInterface -ne 'wlan0') {
    throw 'The supplied systemd network unit is intentionally bound to wlan0.'
}
$HawkBitConfig = Assert-LocalRegularFile 'hawkbit.conf' $HawkBitConfig
$WpaSupplicantConfig = Assert-LocalRegularFile 'wpa_supplicant.conf' $WpaSupplicantConfig
$TrustKeyPath = Assert-LocalRegularFile 'SWU trust key' $TrustKeyPath
$settings = Get-HawkBitSettings $HawkBitConfig
$hawkBitUri = [uri]$settings.HAWKBIT_SERVER_URL
if ($settings.SWUPDATE_TRUST_KEY -ne '/etc/mcu-update/trust/swu-release.pem') {
    throw 'hawkbit.conf SWUPDATE_TRUST_KEY must be /etc/mcu-update/trust/swu-release.pem for this deployment.'
}

$artifacts = foreach ($name in $ArtifactNames) {
    $path = Assert-LocalRegularFile "artifact $name" (Join-Path $ArtifactDirectory $name)
    [pscustomobject]@{ Name = $name; Path = $path; Sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
foreach ($name in $RuntimeScripts) { Assert-LocalRegularFile "runtime script $name" (Join-Path $PSScriptRoot $name) | Out-Null }
foreach ($name in @(
    'mcu-update-wifi.service', 'mcu-update-wpa-supplicant.service', 'mcu-update-network-online.service',
    'mcu-update-token-signer.service', 'mcu-updater.service', 'mcu-update-swupdate.service',
    '80-mcu-update-wlan0.network', 'wifi.conf.example', 'signer.conf.example', 'mcu-updater.conf.example')) {
    Assert-LocalRegularFile "systemd source $name" (Join-Path $UnitRoot $name) | Out-Null
}

if ($ValidateOnly) {
    Write-Output 'Local systemd deployment inputs are valid.'
    return
}
if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) { throw "adb executable does not exist: $AdbPath" }
if (((Invoke-Adb get-state | Select-Object -Last 1).Trim()) -ne 'device') { throw "ADB device is not ready: $Serial" }

# Fail before writing anything when this is the current BusyBox/Android HIL rootfs.
Invoke-AdbShell 'test "$(cat /proc/1/comm)" = systemd'
Invoke-AdbShell 'command -v systemctl >/dev/null && command -v install >/dev/null && command -v nc >/dev/null && test -x /usr/sbin/ip && test -x /usr/sbin/wpa_supplicant && test -x /usr/lib/systemd/systemd-networkd-wait-online'
Invoke-AdbShell 'systemctl cat tee-supplicant.service >/dev/null'
Invoke-AdbShell "test -x /sbin/swupdate && test -d '$RemoteRoot' && test ! -L '$RemoteRoot'"
Invoke-AdbShell "grep -Fq '/run/mcu-update/remote-handler/' /sbin/swupdate"

$localStage = Join-Path ([IO.Path]::GetTempPath()) ('mcu-update-systemd-' + [guid]::NewGuid().ToString('N'))
$remoteStage = '/data/local/tmp/mcu-update-systemd-' + [guid]::NewGuid().ToString('N')
try {
    New-Item -ItemType Directory -LiteralPath $localStage | Out-Null
    $updaterConfig = @(
        'MCU_UPDATE_WORK_ROOT=/run/mcu-update/jobs',
        "MCU_UPDATE_CAN_IFNAME=$CanInterface",
        'MCU_UPDATE_REMOTE_ENDPOINT_BASE=ipc:///run/mcu-update/remote-handler/mcu-v1',
        'MCU_TOKEN_SIGNER_ENDPOINT=/run/mcu-token-signer/v1.sock',
        "MCU_TOKEN_SIGNER_UID=$SignerUid",
        "MCU_TOKEN_SIGNER_GID=$SignerGid",
        "MCU_TOKEN_SIGNER_SOCKET_GID=$SignerSocketGid",
        "MCU_TOKEN_SIGNER_TIMEOUT_MS=$SignerTimeoutMs"
    ) -join "`n"
    $signerConfig = @(
        'MCU_TOKEN_SIGNER_ENDPOINT=/run/mcu-token-signer/v1.sock',
        "MCU_TOKEN_SIGNER_UID=$SignerUid",
        "MCU_TOKEN_SIGNER_GID=$SignerGid",
        "MCU_TOKEN_SIGNER_SOCKET_GID=$SignerSocketGid",
        'MCU_TOKEN_SIGNER_CLIENT_UID=0',
        'MCU_TOKEN_SIGNER_CLIENT_GID=0'
    ) -join "`n"
    Set-Content -LiteralPath (Join-Path $localStage 'mcu-updater.conf') -Value ($updaterConfig + "`n") -Encoding ascii -NoNewline
    Set-Content -LiteralPath (Join-Path $localStage 'signer.conf') -Value ($signerConfig + "`n") -Encoding ascii -NoNewline
    Set-Content -LiteralPath (Join-Path $localStage 'wifi.conf') -Value ("MCU_WIFI_IFNAME=$WifiInterface`n") -Encoding ascii -NoNewline

    Invoke-AdbShell "umask 077; mkdir '$remoteStage'; mkdir '$remoteStage/bin' '$remoteStage/libexec' '$remoteStage/systemd' '$remoteStage/config' '$remoteStage/trust'"
    foreach ($artifact in $artifacts) { Invoke-Adb push $artifact.Path "$remoteStage/bin/$($artifact.Name)" | Out-Null }
    foreach ($name in $RuntimeScripts) { Invoke-Adb push (Join-Path $PSScriptRoot $name) "$remoteStage/libexec/$name" | Out-Null }
    foreach ($name in @('mcu-update-wifi.service', 'mcu-update-wpa-supplicant.service', 'mcu-update-network-online.service', 'mcu-update-token-signer.service', 'mcu-updater.service', 'mcu-update-swupdate.service', '80-mcu-update-wlan0.network')) {
        Invoke-Adb push (Join-Path $UnitRoot $name) "$remoteStage/systemd/$name" | Out-Null
    }
    Invoke-Adb push $HawkBitConfig "$remoteStage/config/hawkbit.conf" | Out-Null
    Invoke-Adb push $WpaSupplicantConfig "$remoteStage/config/wpa_supplicant.conf" | Out-Null
    Invoke-Adb push (Join-Path $localStage 'mcu-updater.conf') "$remoteStage/config/mcu-updater.conf" | Out-Null
    Invoke-Adb push (Join-Path $localStage 'signer.conf') "$remoteStage/config/signer.conf" | Out-Null
    Invoke-Adb push (Join-Path $localStage 'wifi.conf') "$remoteStage/config/wifi.conf" | Out-Null
    Invoke-Adb push $TrustKeyPath "$remoteStage/trust/swu-release.pem" | Out-Null

    foreach ($artifact in $artifacts) {
        $remoteHash = ((Invoke-Adb shell "sha256sum '$remoteStage/bin/$($artifact.Name)'") -join '').Split(' ')[0]
        if ($remoteHash -ne $artifact.Sha256) { throw "Staged artifact hash mismatch: $($artifact.Name)" }
    }

    # An older release used a different unit-name prefix. Discover that prefix
    # from its Remote Handler binding, then remove the complete unit cohort so
    # two update stacks can never remain enabled after an in-place deployment.
    Invoke-AdbShell 'for unit_path in /etc/systemd/system/*-swupdate.service; do test -f "$unit_path" || continue; grep -q "remote-handler" "$unit_path" || continue; unit=${unit_path##*/}; test "$unit" != mcu-update-swupdate.service || continue; prefix=${unit%-swupdate.service}; case "$prefix" in ""|*[!A-Za-z0-9._-]*) exit 1 ;; esac; for legacy_unit in /etc/systemd/system/"$prefix"-*.service; do test -f "$legacy_unit" || continue; legacy_name=${legacy_unit##*/}; systemctl stop "$legacy_name" 2>/dev/null || true; systemctl disable "$legacy_name" 2>/dev/null || true; rm -f "$legacy_unit"; done; rm -f /etc/systemd/network/80-"$prefix"-wlan0.network; for legacy_root in /etc/"$prefix" /usr/libexec/"$prefix" /run/media/mmcblk0p6/"$prefix"; do if test -e "$legacy_root"; then test ! -L "$legacy_root" || exit 1; test ! -e "$legacy_root.retired" || exit 1; mv "$legacy_root" "$legacy_root.retired" || exit 1; fi; done; done; systemctl daemon-reload'
    Invoke-AdbShell 'systemctl stop mcu-update-swupdate.service mcu-updater.service 2>/dev/null || true'
    Invoke-AdbShell "install -d -o root -g root -m 0750 '$RemoteRoot/bin' /usr/libexec/mcu-update /etc/mcu-update /etc/mcu-update/trust /etc/systemd/system /etc/systemd/network"
    Invoke-AdbShell "install -o root -g root -m 0755 '$remoteStage/bin/mcu-updater' '$RemoteRoot/bin/mcu-updater'; install -o root -g root -m 0755 '$remoteStage/bin/token-signer-daemon' '$RemoteRoot/bin/token-signer-daemon'; install -o root -g root -m 0755 '$remoteStage/bin/gateway-lss-master' '$RemoteRoot/bin/gateway-lss-master'; install -d -o root -g root -m 0700 /var/lib/mcu-update/devices /run/mcu-update"
    Invoke-AdbShell "install -o root -g root -m 0755 '$remoteStage/libexec/swupdate-suricatta' /usr/libexec/mcu-update/swupdate-suricatta; install -o root -g root -m 0755 '$remoteStage/libexec/wait-for-remote-handler' /usr/libexec/mcu-update/wait-for-remote-handler; install -o root -g root -m 0755 '$remoteStage/libexec/wait-for-token-signer' /usr/libexec/mcu-update/wait-for-token-signer"
    Invoke-AdbShell "install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-wifi.service' /etc/systemd/system/mcu-update-wifi.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-wpa-supplicant.service' /etc/systemd/system/mcu-update-wpa-supplicant.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-network-online.service' /etc/systemd/system/mcu-update-network-online.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-token-signer.service' /etc/systemd/system/mcu-update-token-signer.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-updater.service' /etc/systemd/system/mcu-updater.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-swupdate.service' /etc/systemd/system/mcu-update-swupdate.service; install -o root -g root -m 0644 '$remoteStage/systemd/80-mcu-update-wlan0.network' /etc/systemd/network/80-mcu-update-wlan0.network"
    Invoke-AdbShell "install -o root -g root -m 0600 '$remoteStage/config/hawkbit.conf' /etc/mcu-update/hawkbit.conf; install -o root -g root -m 0600 '$remoteStage/config/wpa_supplicant.conf' /etc/mcu-update/wpa_supplicant.conf; install -o root -g root -m 0640 '$remoteStage/config/mcu-updater.conf' /etc/mcu-update/mcu-updater.conf; install -o root -g root -m 0640 '$remoteStage/config/signer.conf' /etc/mcu-update/signer.conf; install -o root -g root -m 0640 '$remoteStage/config/wifi.conf' /etc/mcu-update/wifi.conf; install -o root -g root -m 0400 '$remoteStage/trust/swu-release.pem' /etc/mcu-update/trust/swu-release.pem"
    Invoke-AdbShell "rm -rf '$remoteStage'; systemctl daemon-reload; systemctl enable systemd-networkd.service mcu-update-wifi.service mcu-update-wpa-supplicant.service mcu-update-token-signer.service mcu-updater.service mcu-update-swupdate.service"
    Invoke-AdbShell 'systemctl restart systemd-networkd.service mcu-update-wifi.service mcu-update-wpa-supplicant.service mcu-update-network-online.service'
    Test-BoardTcpReachability -HostName $hawkBitUri.Host -Port $hawkBitUri.Port
    Invoke-AdbShell 'systemctl restart mcu-update-token-signer.service mcu-updater.service mcu-update-swupdate.service'
    Invoke-AdbShell 'systemctl is-active --quiet mcu-update-wpa-supplicant.service mcu-update-token-signer.service mcu-updater.service mcu-update-swupdate.service'
}
finally {
    if (Test-Path -LiteralPath $localStage) { Remove-Item -LiteralPath $localStage -Recurse -Force }
    if ($remoteStage) { Invoke-AdbResult shell "rm -rf '$remoteStage'" | Out-Null }
}

Write-Output 'MCU update systemd runtime is deployed and active.'
Write-Output 'Runtime configuration is release-independent; each verified image creates its own transaction.'
