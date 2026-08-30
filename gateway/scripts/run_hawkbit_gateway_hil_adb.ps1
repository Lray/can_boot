[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [string]$ArtifactDirectory = 'E:\T527\can_boot\gateway\build-target-hawkbit-e2e',
    [Parameter(Mandatory = $true)][string]$SwuPath,
    [Parameter(Mandatory = $true)][string]$ImagePath,
    [string]$RemoteDirectory = '/data/local/tmp/ecu-ota-hawkbit-runtime',
    [string]$WslDistribution = 'Ubuntu-24.04',
    [string]$WslProjectDirectory = '/mnt/e/T527/can_boot/gateway',
    [string]$HawkBitHost,
    [string]$WifiSsid = '10086',
    [int]$TimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$TrustKey = '/etc/ecu-ota/trust/swu-release-adb-rsapss-v1.pem'
$RemoteEndpointPath = '/run/ecu-ota/remote-handler/ecu-v1'
$SignerEndpointPath = '/run/ecu-token-signer/v1.sock'
$HawkBitStateDirectory = '/home/lirui/.local/state/ecu-ota-hawkbit'

if ($Serial -notmatch '^[A-Za-z0-9._:-]+$') {
    throw 'Device serial contains unsupported characters.'
}
if ($RemoteDirectory -notmatch '^/data/local/tmp/[a-z0-9._/-]+$' -or
    $RemoteDirectory.Contains('..') -or $RemoteDirectory.Contains('//') -or
    $RemoteDirectory.EndsWith('/')) {
    throw 'Remote directory must be a fixed child of /data/local/tmp.'
}
if ($WifiSsid -notmatch '^[A-Za-z0-9._-]{1,32}$') {
    throw 'Wi-Fi SSID contains unsupported characters.'
}
if ($TimeoutSeconds -lt 30 -or $TimeoutSeconds -gt 300) {
    throw 'TimeoutSeconds must be between 30 and 300.'
}
if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) {
    throw "adb executable does not exist: $AdbPath"
}
if (-not (Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) {
    throw "Artifact directory does not exist: $ArtifactDirectory"
}
if (-not (Test-Path -LiteralPath $SwuPath -PathType Leaf)) {
    throw "SWU artifact does not exist: $SwuPath"
}
if (-not (Test-Path -LiteralPath $ImagePath -PathType Leaf)) {
    throw "release image does not exist: $ImagePath"
}
$image = Get-Item -LiteralPath $ImagePath
if ($image.Length -lt 1 -or $image.Length -gt 0x00020000) {
    throw 'Release image size must be between 1 and 131072 bytes.'
}
$imageSize = [uint64]$image.Length
$imageSha256 = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash.ToLowerInvariant()

function Invoke-AdbResult {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $AdbPath -s $Serial @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
    [pscustomobject]@{ ExitCode = $exitCode; Output = @($output) }
}

function Invoke-Adb {
    param(
        [switch]$Sensitive,
        [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments
    )

    $result = Invoke-AdbResult @Arguments
    if ($result.ExitCode -ne 0) {
        $displayArguments = if ($Sensitive) { '<redacted>' } else { $Arguments -join ' ' }
        throw "adb failed ($($result.ExitCode)): $displayArguments`n$($result.Output -join "`n")"
    }
    $result.Output
}

function Invoke-Wsl {
    param([Parameter(Mandatory = $true)][string]$Command)

    $script = '/tmp/hil-' + [guid]::NewGuid().ToString('N') + '.sh'
    $uncPath = '\\wsl.localhost\' + $WslDistribution + $script.Replace('/', '\')
    Set-Content -LiteralPath $uncPath -Value $Command -Encoding ascii -NoNewline
    try {
        $output = & wsl.exe -d $WslDistribution -- bash $script 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "WSL command failed: $Command`n$($output -join "`n")"
        }
        @($output)
    }
    finally {
        & wsl.exe -d $WslDistribution -- rm -f $script 2>&1 | Out-Null
    }
}

function Get-HawkBitHost {
    param([Parameter(Mandatory = $true)][string]$BoardIp)

    if (-not [string]::IsNullOrEmpty($HawkBitHost)) {
        if ($HawkBitHost -notmatch '^[0-9]{1,3}(\.[0-9]{1,3}){3}$') {
            throw 'HawkBitHost must be an IPv4 address.'
        }
        return $HawkBitHost
    }

    $prefix = (($BoardIp -split '\.')[0..2] -join '.') + '.'
    $candidates = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
        Where-Object { $_.IPAddress.StartsWith($prefix) -and $_.IPAddress -ne $BoardIp } |
        Select-Object -ExpandProperty IPAddress -Unique
    foreach ($candidate in $candidates) {
        $probe = Invoke-AdbResult shell "busybox wget -q -S -O /dev/null 'http://$candidate:18080/' 2>&1"
        if (($probe.Output -join "`n") -match 'HTTP/1\.[01] (200|302|401)') {
            return $candidate
        }
    }
    throw 'Unable to discover a board-reachable hawkBit host. Supply -HawkBitHost.'
}

function Read-HawkBitState {
    param([Parameter(Mandatory = $true)][string]$StateFile)

    if ($StateFile -notmatch '^/home/lirui/\.local/state/ecu-ota-hawkbit/[A-Za-z0-9._-]+\.env$') {
        throw 'hawkBit state path is invalid.'
    }
    $lines = @((Invoke-Wsl "set -a; . '$StateFile'; set +a; printf '%s\n' `"`$DEVICE_URL`" `"`$TARGET_ID`" `"`$TARGET_TOKEN`" `"`$ACTION_ID`" `"`$ARTIFACT_SHA256`"") | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
    if ($lines.Count -ne 5 -or $lines[1] -notmatch '^t527-hawkbit-gateway-' -or
        $lines[2] -notmatch '^[0-9a-f]{32}$' -or $lines[3] -notmatch '^\d+$') {
        throw 'hawkBit state is invalid.'
    }
    [pscustomobject]@{
        DeviceUrl = $lines[0]
        TargetId = $lines[1]
        TargetToken = $lines[2]
        ActionId = $lines[3]
        ArtifactSha256 = $lines[4]
    }
}

function Get-HawkBitStatus {
    param([Parameter(Mandatory = $true)][string]$StateFile)

    $lines = Invoke-Wsl "cd '$WslProjectDirectory' && ./scripts/hawkbit_action_wsl.sh status --state '$StateFile'"
    $status = @{}
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            $status[$Matches[1]] = $Matches[2]
        }
    }
    $status
}

function Stop-HostedProcess {
    param([AllowNull()][System.Diagnostics.Process]$Process)

    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
    }
}

$orchestratorProcess = $null
$stateFile = $null
try {
    $state = (Invoke-Adb get-state | Select-Object -Last 1).Trim()
    if ($state -ne 'device') {
        throw "ADB device is not ready: serial=$Serial state=$state"
    }

    $boardIp = ((Invoke-Adb shell "ip -4 addr show wlan0 | sed -n 's/.*inet \([0-9.]*\)\/.*/\1/p' | head -1") -join '').Trim()
    if ($boardIp -notmatch '^[0-9]{1,3}(\.[0-9]{1,3}){3}$') {
        throw 'Board has no Wi-Fi IPv4 address yet.'
    }
    $resolvedHawkBitHost = Get-HawkBitHost -BoardIp $boardIp
    $wslArtifact = (Invoke-Wsl "wslpath -a '$SwuPath'" | Select-Object -Last 1).Trim()
    $stateFile = "$HawkBitStateDirectory/$([guid]::NewGuid().ToString()).env"

    & wsl.exe -d $WslDistribution -- bash -lc (
        "cd '$WslProjectDirectory' && ./scripts/hawkbit_action_wsl.sh register " +
        "--hawkbit-host '$resolvedHawkBitHost' --target-address '$boardIp' --state '$stateFile'"
    ) | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'hawkBit target registration failed.'
    }
    & wsl.exe -d $WslDistribution -- bash -lc (
        "cd '$WslProjectDirectory' && ./scripts/hawkbit_action_wsl.sh assign " +
        "--artifact '$wslArtifact' --state '$stateFile'"
    ) | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw 'hawkBit action assignment failed.'
    }
    $hawkBit = Read-HawkBitState -StateFile $stateFile
    if ($hawkBit.ArtifactSha256 -ne (Get-FileHash -LiteralPath $SwuPath -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'hawkBit artifact hash does not match the selected SWU.'
    }

    & "$PSScriptRoot\deploy_ecu_ota_runtime_adb.ps1" -AdbPath $AdbPath -Serial $Serial `
        -ArtifactDirectory $ArtifactDirectory -WifiSsid $WifiSsid

    $signerUid = ((Invoke-Adb shell "awk -F: '/^ecu-token-signer:/{print `$3; exit}' /etc/passwd") -join '').Trim()
    $signerGid = ((Invoke-Adb shell "awk -F: '/^ecu-token-signer:/{print `$4; exit}' /etc/passwd") -join '').Trim()
    $signerSocketGid = ((Invoke-Adb shell "awk -F: '/^ecu-token-client:/{print `$3; exit}' /etc/group") -join '').Trim()
    if ($signerUid -notmatch '^[1-9][0-9]{0,4}$' -or
        $signerGid -notmatch '^[1-9][0-9]{0,4}$' -or
        $signerSocketGid -notmatch '^[1-9][0-9]{0,4}$') {
        throw 'The deployed token signer account or socket group is invalid.'
    }
    Invoke-Adb shell (
        "test -x /sbin/swupdate; test -x /run/media/mmcblk0p6/ecu-ota/bin/token-signer-daemon; " +
        "test -r '$TrustKey'; test -e /sys/class/net/awlink0"
    ) | Out-Null

    $jobId = [guid]::NewGuid().ToString()
    $orchestratorLog = "$RemoteDirectory/$jobId.orchestrator.log"
    $orchestratorCommand =
        "install -d -o root -g root -m 0750 /run/ecu-ota /run/ecu-ota/remote-handler; " +
        "rm -f '$RemoteEndpointPath' '$RemoteEndpointPath.lock'; rm -rf '$RemoteDirectory/$jobId'; " +
        "'$RemoteDirectory/ecu-ota-orchestrator' --job-id '$jobId' " +
        "--work-root '$RemoteDirectory' --wifi-config /run/media/mmcblk0p6/ecu-ota/config/wpa_supplicant.conf " +
        "--wifi-ssid '$WifiSsid' --expected-size '$imageSize' --expected-sha256 '$imageSha256' " +
        "--hawkbit-url 'http://${resolvedHawkBitHost}:18080' --hawkbit-id '$($hawkBit.TargetId)' " +
        "--hawkbit-token '$($hawkBit.TargetToken)' --trust-key '$TrustKey' --ifname awlink0 " +
        "--signer-endpoint '$SignerEndpointPath' " +
        "--signer-uid '$signerUid' --signer-gid '$signerGid' " +
        "--signer-socket-gid '$signerSocketGid' --signer-timeout-ms 1000 " +
        "> '$orchestratorLog' 2>&1"
    $runScript = "$env:TEMP\opencode\run-orch-$jobId.sh"
    Set-Content -LiteralPath $runScript -Value $orchestratorCommand -Encoding ascii -NoNewline
    Invoke-Adb push $runScript "/data/local/tmp/run-orch-$jobId.sh" | Out-Null
    Invoke-Adb shell "chmod 0700 '/data/local/tmp/run-orch-$jobId.sh'" | Out-Null
    $orchestratorProcess = Start-Process -FilePath $AdbPath -ArgumentList @(
        '-s', $Serial, 'shell', "sh '/data/local/tmp/run-orch-$jobId.sh'"
    ) -PassThru -WindowStyle Hidden

    $terminalStatus = $null
    for ($attempt = 0; $attempt -lt 180; $attempt++) {
        $terminalStatus = Get-HawkBitStatus -StateFile $stateFile
        if ($terminalStatus['ACTION_STATUS'] -eq 'finished' -and $terminalStatus['ACTION_ACTIVE'] -eq 'false') {
            break
        }
        Start-Sleep -Seconds 1
    }
    if ($terminalStatus['ACTION_STATUS'] -ne 'finished' -or $terminalStatus['ACTION_ACTIVE'] -ne 'false') {
        throw 'hawkBit did not receive the official successful terminal state.'
    }

    $publishedHash = ((Invoke-Adb shell "sha256sum '$RemoteDirectory/$jobId/package-input-v1/image.bin'") -join "`n").Split(' ')[0]
    if ($publishedHash -ne $imageSha256) {
        throw 'Published image does not match the release image.'
    }
    $jobFiles = Invoke-Adb shell "ls -ln '$RemoteDirectory/$jobId' '$RemoteDirectory/$jobId/package-input-v1'"
    [pscustomobject]@{
        TargetId = $hawkBit.TargetId
        ActionId = $hawkBit.ActionId
        ActionStatus = $terminalStatus['ACTION_STATUS']
        TargetStatus = $terminalStatus['TARGET_UPDATE_STATUS']
        HawkBitHost = $resolvedHawkBitHost
        JobDirectory = "$RemoteDirectory/$jobId"
        OrchestratorLog = $orchestratorLog
        Evidence = ($jobFiles -join "`n")
    }
}
finally {
    Stop-HostedProcess -Process $orchestratorProcess
    Invoke-AdbResult shell "killall swupdate 2>/dev/null || true; rm -f '$RemoteEndpointPath' '$RemoteEndpointPath.lock'" | Out-Null
    if ($runScript) { Invoke-AdbResult shell "rm -f '/data/local/tmp/run-orch-$jobId.sh'" | Out-Null }
}
