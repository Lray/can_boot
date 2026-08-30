[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [string]$ArtifactDirectory = 'E:\T527\can_boot\gateway\build-target-hawkbit-e2e',
    [string]$WifiSsid = '10086',
    [string]$RemoteTrustKey = '/etc/ecu-ota/trust/swu-release-adb-rsapss-v1.pem',
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RemoteRoot = '/run/media/mmcblk0p6/ecu-ota'
$ArtifactNames = @('ecu-ota-orchestrator', 'gateway-ota-worker-v1', 'token-signer-daemon')

if ($PSVersionTable.PSVersion.Major -lt 5) { throw 'PowerShell 5 or later is required.' }
if ($Serial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'Device serial contains unsupported characters.' }
if ($WifiSsid -notmatch '^[A-Za-z0-9._-]{1,32}$') { throw 'WifiSsid contains unsupported characters.' }
if ($RemoteTrustKey -notmatch '^/[A-Za-z0-9._/-]+$' -or $RemoteTrustKey.Contains('..') -or
    $RemoteTrustKey.Contains('//')) { throw 'RemoteTrustKey is not a safe absolute path.' }
if (-not (Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) {
    throw "Artifact directory does not exist: $ArtifactDirectory"
}

$Artifacts = foreach ($Name in $ArtifactNames) {
    $Path = Join-Path $ArtifactDirectory $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required artifact does not exist: $Path" }
    $File = Get-Item -LiteralPath $Path
    [pscustomobject]@{
        Name = $Name
        Path = $File.FullName
        Size = $File.Length
        Sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

if ($ValidateOnly) {
    $Artifacts | Format-Table Name, Size, Sha256 -AutoSize
    return
}
if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) { throw "adb executable does not exist: $AdbPath" }

function Invoke-AdbResult {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $PreviousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $Output = & $AdbPath -s $Serial @Arguments 2>&1
        $ExitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $PreviousErrorAction }
    [pscustomobject]@{ ExitCode = $ExitCode; Output = @($Output) }
}

function Invoke-Adb {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $Result = Invoke-AdbResult @Arguments
    if ($Result.ExitCode -ne 0) {
        throw "adb failed ($($Result.ExitCode)): $($Arguments -join ' ')`n$($Result.Output -join "`n")"
    }
    $Result.Output
}

function Write-AsciiFile {
    param([string]$RemotePath, [string]$Content)
    Invoke-AdbInput "umask 077; cat > '$RemotePath'" $Content | Out-Null
}

function Invoke-AdbInput {
    param([string]$Command, [string]$InputText)

    $StartInfo = New-Object Diagnostics.ProcessStartInfo
    $StartInfo.FileName = $AdbPath
    $StartInfo.Arguments = "-s `"$Serial`" exec-out sh -c `"$Command`""
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardInput = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Process = New-Object Diagnostics.Process
    $Process.StartInfo = $StartInfo
    if (-not $Process.Start()) { throw 'Failed to start adb.' }
    $Process.StandardInput.Write($InputText)
    $Process.StandardInput.Close()
    $Output = $Process.StandardOutput.ReadToEnd()
    $ErrorOutput = $Process.StandardError.ReadToEnd()
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) { throw "adb failed ($($Process.ExitCode)).`n$ErrorOutput" }
    $Output
}

function Get-WpaPsk {
    $Password = [Environment]::GetEnvironmentVariable('ECU_OTA_WIFI_PASSWORD', 'Process')
    if ([string]::IsNullOrEmpty($Password) -or $Password -notmatch '^[\x20-\x7e]{8,63}$') {
        throw 'Set ECU_OTA_WIFI_PASSWORD to the WPA passphrase for initial Wi-Fi provisioning.'
    }
    $PskLine = (Invoke-AdbInput (
        "wpa_passphrase '$WifiSsid' | sed -n " +
        "'s/^[[:space:]]*//; /^psk=[0-9a-f]\{64\}$/p'"
    ) ($Password + "`n")).Trim()
    if ($PskLine -notmatch '^psk=([0-9a-f]{64})$') {
        throw 'Board wpa_passphrase did not generate one valid WPA-PSK.'
    }
    $Matches[1]
}

$State = (Invoke-Adb get-state | Select-Object -Last 1).Trim()
if ($State -ne 'device') { throw "ADB device is not ready: serial=$Serial state=$State" }

Invoke-Adb shell (
    "grep -q '^ecu-token-client:' /etc/group || addgroup -S ecu-token-client; " +
    "grep -q '^ecu-token-signer:' /etc/group || addgroup -S ecu-token-signer; " +
    "grep -q '^ecu-token-signer:' /etc/passwd || " +
    "adduser -S -D -H -G ecu-token-signer ecu-token-signer; " +
    "awk -F: '`$1 == 'ecu-token-signer' && `$3 > 0 { found = 1 } END { exit found ? 0 : 1 }' /etc/passwd; " +
    "awk -F: '`$1 == 'ecu-token-client' && `$3 > 0 { found = 1 } END { exit found ? 0 : 1 }' /etc/group"
) | Out-Null

$WifiConfig = "$RemoteRoot/config/wpa_supplicant.conf"
$WifiProbe = Invoke-AdbResult shell (
    "if [ ! -e '$WifiConfig' ]; then exit 10; fi; " +
    "if [ -f '$WifiConfig' ] && [ ! -L '$WifiConfig' ] && " +
    "[ `"`$(stat -c '%u:%a' '$WifiConfig')`" = '0:600' ]; then exit 0; fi; exit 11"
)
$ProvisionWifi = switch ($WifiProbe.ExitCode) {
    0 { $false }
    10 { $true }
    default { throw 'Existing persistent Wi-Fi configuration is unsafe.' }
}

$StageName = '.deploy-' + [guid]::NewGuid().ToString('N')
$RemoteStage = "$RemoteRoot/$StageName"
try {
    if ($ProvisionWifi) {
        $Psk = Get-WpaPsk
        $WifiConfigContent = (@(
            'ctrl_interface=DIR=/run/wpa_supplicant GROUP=0'
            'update_config=1'
            ''
            'network={'
            "    ssid=`"$WifiSsid`""
            "    psk=$Psk"
            '    key_mgmt=WPA-PSK'
            '    priority=10'
            '}'
        ) -join "`n") + "`n"
    }

    Invoke-Adb shell (
        "[ -d /run/media/mmcblk0p6 ] && [ ! -L /run/media/mmcblk0p6 ] && " +
        "[ `"`$(stat -c '%u' /run/media/mmcblk0p6)`" = 0 ]; " +
        "! start-stop-daemon -K -t -x /sbin/swupdate >/dev/null 2>&1; " +
        "! start-stop-daemon -K -t -x '$RemoteRoot/bin/ecu-ota-orchestrator' >/dev/null 2>&1; " +
        "if [ -e '$RemoteRoot' ]; then [ -d '$RemoteRoot' ] && [ ! -L '$RemoteRoot' ]; " +
        "else mkdir '$RemoteRoot'; fi; " +
        "for dir in bin config trust var; do [ ! -e '$RemoteRoot/'`"`$dir`" ] || " +
        "{ [ -d '$RemoteRoot/'`"`$dir`" ] && [ ! -L '$RemoteRoot/'`"`$dir`" ]; }; done; " +
        "mkdir -p '$RemoteRoot/bin' '$RemoteRoot/config' '$RemoteRoot/trust' " +
        "'$RemoteRoot/var/jobs'; " +
        "for dir in '$RemoteRoot' '$RemoteRoot/bin' '$RemoteRoot/config' '$RemoteRoot/trust' " +
        "'$RemoteRoot/var' '$RemoteRoot/var/jobs'; do " +
        "[ -d `"`$dir`" ] && [ ! -L `"`$dir`" ] && [ `"`$(stat -c '%u' `"`$dir`" )`" = 0 ]; done; " +
        "mkdir '$RemoteStage'; mkdir '$RemoteStage/bin' '$RemoteStage/config' '$RemoteStage/trust'; " +
        "chmod 0700 '$RemoteStage' '$RemoteStage/bin' '$RemoteStage/config' '$RemoteStage/trust' " +
        "'$RemoteRoot/config' '$RemoteRoot/trust' '$RemoteRoot/var' '$RemoteRoot/var/jobs'; " +
        "chmod 0750 '$RemoteRoot' '$RemoteRoot/bin'; " +
        "[ -f '$RemoteTrustKey' ] && [ ! -L '$RemoteTrustKey' ] && " +
        "[ `"`$(stat -c '%u' '$RemoteTrustKey')`" = 0 ]; " +
        "cp '$RemoteTrustKey' '$RemoteStage/trust/swu-release.pem'"
    ) | Out-Null

    foreach ($Artifact in $Artifacts) {
        Invoke-Adb push $Artifact.Path "$RemoteStage/bin/$($Artifact.Name)" | Out-Null
    }
    if ($ProvisionWifi) { Write-AsciiFile "$RemoteStage/config/wpa_supplicant.conf" $WifiConfigContent }

    Invoke-Adb shell (
        "chown -R root:root '$RemoteStage'; chmod 0700 '$RemoteStage/bin/'*; " +
        "chmod 0400 '$RemoteStage/trust/swu-release.pem'; " +
        $(if ($ProvisionWifi) { "chmod 0600 '$RemoteStage/config/wpa_supplicant.conf'; " } else { '' }) +
        "sync"
    ) | Out-Null

    foreach ($Artifact in $Artifacts) {
        $RemoteHash = ((Invoke-Adb shell "sha256sum '$RemoteStage/bin/$($Artifact.Name)'") -join "`n").Split(' ')[0]
        if ($RemoteHash -ne $Artifact.Sha256) { throw "Remote hash mismatch: $($Artifact.Name)" }
    }

    Invoke-Adb shell (
        "for name in ecu-ota-orchestrator gateway-ota-worker-v1 token-signer-daemon; do " +
        "[ ! -e '$RemoteRoot/bin/'`"`$name`" ] || { [ -f '$RemoteRoot/bin/'`"`$name`" ] && " +
        "[ ! -L '$RemoteRoot/bin/'`"`$name`" ]; }; done; " +
        "mv -f '$RemoteStage/bin/ecu-ota-orchestrator' '$RemoteRoot/bin/ecu-ota-orchestrator'; " +
        "mv -f '$RemoteStage/bin/gateway-ota-worker-v1' '$RemoteRoot/bin/gateway-ota-worker-v1'; " +
        "mv -f '$RemoteStage/bin/token-signer-daemon' '$RemoteRoot/bin/token-signer-daemon'; " +
        "mv -f '$RemoteStage/trust/swu-release.pem' '$RemoteRoot/trust/swu-release.pem'; " +
        "rm -f '$RemoteRoot/config/swupdate.cfg'; " +
        $(if ($ProvisionWifi) { "mv -f '$RemoteStage/config/wpa_supplicant.conf' '$WifiConfig'; " } else { '' }) +
        "rmdir '$RemoteStage/bin' '$RemoteStage/config' '$RemoteStage/trust' '$RemoteStage'; sync"
    ) | Out-Null
}
finally {
    Invoke-AdbResult shell (
        "rm -f '$RemoteStage/bin/ecu-ota-orchestrator' '$RemoteStage/bin/gateway-ota-worker-v1' " +
        "'$RemoteStage/bin/token-signer-daemon' " +
        "'$RemoteStage/config/wpa_supplicant.conf' " +
        "'$RemoteStage/trust/swu-release.pem'; " +
        "rmdir '$RemoteStage/bin' '$RemoteStage/config' '$RemoteStage/trust' '$RemoteStage' 2>/dev/null || true"
    ) | Out-Null
}

$Artifacts | Format-Table Name, Size, Sha256 -AutoSize
Write-Output "Persistent Gateway OTA application published: $RemoteRoot"
Write-Output "Wi-Fi configuration: $(if ($ProvisionWifi) { 'created' } else { 'preserved' })"
