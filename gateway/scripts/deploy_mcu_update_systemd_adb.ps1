[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [string]$ArtifactDirectory = 'E:\T527\can_boot\gateway\build-target-hawkbit-e2e',
    [Parameter(Mandatory = $true)][string]$HawkBitConfig,
    [Parameter(Mandatory = $true)][string]$WpaSupplicantConfig,
    [Parameter(Mandatory = $true)][string]$TrustKeyPath,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ArtifactNames = @('LSS_gateway', 'mcu-updater')
$RemoteRoot = '/run/media/mmcblk0p6/mcu-update'
$ConfigRoot = Join-Path $PSScriptRoot '..\config'
$UnitRoot = Join-Path $ConfigRoot 'systemd'
$RuntimeScripts = @('swupdate-suricatta', 'swupdate-restart', 'wait-for-remote-handler')

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
    'LSS_gateway.service', 'mcu-updater.service', 'mcu-update-swupdate.service',
    '80-mcu-update-wlan0.network')) {
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
Invoke-AdbShell "grep -aFq '/run/mcu-update/swupdate-ctrl.sock' /sbin/swupdate && grep -aFq '/run/mcu-update/remote-handler/' /sbin/swupdate"

$localStage = Join-Path ([IO.Path]::GetTempPath()) ('mcu-update-systemd-' + [guid]::NewGuid().ToString('N'))
$remoteStage = '/data/local/tmp/mcu-update-systemd-' + [guid]::NewGuid().ToString('N')
try {
    New-Item -ItemType Directory -LiteralPath $localStage | Out-Null
    Invoke-AdbShell "umask 077; mkdir '$remoteStage'; mkdir '$remoteStage/bin' '$remoteStage/libexec' '$remoteStage/systemd' '$remoteStage/config' '$remoteStage/trust'"
    foreach ($artifact in $artifacts) { Invoke-Adb push $artifact.Path "$remoteStage/bin/$($artifact.Name)" | Out-Null }
    foreach ($name in $RuntimeScripts) { Invoke-Adb push (Join-Path $PSScriptRoot $name) "$remoteStage/libexec/$name" | Out-Null }
    foreach ($name in @('mcu-update-wifi.service', 'mcu-update-wpa-supplicant.service', 'mcu-update-network-online.service', 'LSS_gateway.service', 'mcu-updater.service', 'mcu-update-swupdate.service', '80-mcu-update-wlan0.network')) {
        Invoke-Adb push (Join-Path $UnitRoot $name) "$remoteStage/systemd/$name" | Out-Null
    }
    Invoke-Adb push $HawkBitConfig "$remoteStage/config/hawkbit.conf" | Out-Null
    Invoke-Adb push $WpaSupplicantConfig "$remoteStage/config/wpa_supplicant.conf" | Out-Null
    Invoke-Adb push $TrustKeyPath "$remoteStage/trust/swu-release.pem" | Out-Null

    foreach ($artifact in $artifacts) {
        $remoteHash = ((Invoke-Adb shell "sha256sum '$remoteStage/bin/$($artifact.Name)'") -join '').Split(' ')[0]
        if ($remoteHash -ne $artifact.Sha256) { throw "Staged artifact hash mismatch: $($artifact.Name)" }
    }

    Invoke-AdbShell 'systemctl stop mcu-update-swupdate.service mcu-updater.service 2>/dev/null || true'
    Invoke-AdbShell 'systemctl stop LSS_gateway.service 2>/dev/null || true'
    Invoke-AdbShell "install -d -o root -g root -m 0750 '$RemoteRoot/bin' /usr/libexec/mcu-update /etc/mcu-update /etc/mcu-update/trust /etc/systemd/system /etc/systemd/network"
    foreach ($artifact in $artifacts) {
        Invoke-AdbShell "install -o root -g root -m 0755 '$remoteStage/bin/$($artifact.Name)' '$RemoteRoot/bin/$($artifact.Name)'"
    }
    Invoke-AdbShell "install -o root -g root -m 0755 '$remoteStage/libexec/swupdate-suricatta' /usr/libexec/mcu-update/swupdate-suricatta; install -o root -g root -m 0755 '$remoteStage/libexec/wait-for-remote-handler' /usr/libexec/mcu-update/wait-for-remote-handler"
    Invoke-AdbShell "install -o root -g root -m 0755 '$remoteStage/libexec/swupdate-restart' /usr/libexec/mcu-update/swupdate-restart; rm -f /usr/libexec/mcu-update/swupdate-enter-wait"
    Invoke-AdbShell "install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-wifi.service' /etc/systemd/system/mcu-update-wifi.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-wpa-supplicant.service' /etc/systemd/system/mcu-update-wpa-supplicant.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-network-online.service' /etc/systemd/system/mcu-update-network-online.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-updater.service' /etc/systemd/system/mcu-updater.service; install -o root -g root -m 0644 '$remoteStage/systemd/mcu-update-swupdate.service' /etc/systemd/system/mcu-update-swupdate.service; install -o root -g root -m 0644 '$remoteStage/systemd/80-mcu-update-wlan0.network' /etc/systemd/network/80-mcu-update-wlan0.network"
    Invoke-AdbShell "install -o root -g root -m 0644 '$remoteStage/systemd/LSS_gateway.service' /etc/systemd/system/LSS_gateway.service"
    Invoke-AdbShell "install -o root -g root -m 0600 '$remoteStage/config/hawkbit.conf' /etc/mcu-update/hawkbit.conf; install -o root -g root -m 0600 '$remoteStage/config/wpa_supplicant.conf' /etc/mcu-update/wpa_supplicant.conf; install -o root -g root -m 0400 '$remoteStage/trust/swu-release.pem' /etc/mcu-update/trust/swu-release.pem"
    Invoke-AdbShell "rm -rf '$remoteStage'; systemctl daemon-reload; systemctl enable systemd-networkd.service mcu-update-wifi.service mcu-update-wpa-supplicant.service LSS_gateway.service mcu-updater.service mcu-update-swupdate.service"
    Invoke-AdbShell 'systemctl restart systemd-networkd.service mcu-update-wifi.service mcu-update-wpa-supplicant.service mcu-update-network-online.service'
    Test-BoardTcpReachability -HostName $hawkBitUri.Host -Port $hawkBitUri.Port
    Invoke-AdbShell 'systemctl restart LSS_gateway.service mcu-updater.service mcu-update-swupdate.service'
    Invoke-AdbShell 'systemctl is-active --quiet mcu-update-wpa-supplicant.service LSS_gateway.service mcu-updater.service mcu-update-swupdate.service'
}
finally {
    if (Test-Path -LiteralPath $localStage) { Remove-Item -LiteralPath $localStage -Recurse -Force }
    if ($remoteStage) { Invoke-AdbResult shell "rm -rf '$remoteStage'" | Out-Null }
}

Write-Output 'MCU update systemd runtime is deployed and active.'
Write-Output 'Runtime configuration is release-independent; each verified image creates its own transaction.'
