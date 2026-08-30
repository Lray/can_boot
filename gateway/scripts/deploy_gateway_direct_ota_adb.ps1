[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [string]$ArtifactDirectory = 'E:\T527\can_boot\gateway\build-target-direct-4eb0bba-make',
    [string]$RemoteRoot = '/run/media/mmcblk0p6/ecu-ota',
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ArtifactNames = @('gateway-ota-worker-v1', 'token-signer-daemon')
$SignerUid = 200
$SignerGid = 200
$SignerSocketGid = 201

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
if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) {
    throw "adb executable does not exist: $AdbPath"
}
if (-not (Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) {
    throw "Artifact directory does not exist: $ArtifactDirectory"
}

$artifacts = foreach ($name in $ArtifactNames) {
    $item = Assert-LocalRegularFile "artifact $name" (Join-Path $ArtifactDirectory $name)
    if ($item.Length -le 0) {
        throw "artifact $name is empty."
    }
    [pscustomobject]@{
        Name = $name
        Path = $item.FullName
        Size = $item.Length
        Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

if ($ValidateOnly) {
    $artifacts | Format-Table Name, Size, Sha256 -AutoSize
    Write-Output 'Direct Gateway OTA deployment inputs are valid.'
    return
}
if (((Invoke-Adb get-state | Select-Object -Last 1).Trim()) -ne 'device') {
    throw "ADB device is not ready: $Serial"
}

$remoteBin = "$RemoteRoot/bin"
$deploymentId = [guid]::NewGuid().ToString('N')
$remoteStage = "$RemoteRoot/.direct-deploy-$deploymentId"
$remoteBackup = "$RemoteRoot/backup/direct-$deploymentId"
$published = $false

try {
    # This path deliberately refuses to interfere with the HawkBit/SWUpdate path.
    Invoke-Adb shell (
        "set -eu; " +
        "test `"`$(id -u)`" = 0; " +
        "test -d '$RemoteRoot' && test ! -L '$RemoteRoot'; " +
        "test -d '$remoteBin' && test ! -L '$remoteBin'; " +
        "! start-stop-daemon -K -t -x /sbin/swupdate >/dev/null 2>&1; " +
        "! start-stop-daemon -K -t -x '$remoteBin/ecu-ota-orchestrator' >/dev/null 2>&1; " +
        "test -c /dev/tee0; " +
        "test -r /usr/lib64/libteec.so.1 && test -r /usr/lib64/libcrypto.so.1.1; " +
        "awk -F: '`$3 == $SignerUid { found = 1 } END { exit found ? 0 : 1 }' /etc/passwd; " +
        "awk -F: '`$3 == $SignerGid { found = 1 } END { exit found ? 0 : 1 }' /etc/group; " +
        "awk -F: '`$3 == $SignerSocketGid { found = 1 } END { exit found ? 0 : 1 }' /etc/group; " +
        "for name in gateway-ota-worker-v1 token-signer-daemon; do " +
        "[ ! -e '$remoteBin/'`"`$name`" ] || { [ -f '$remoteBin/'`"`$name`" ] && [ ! -L '$remoteBin/'`"`$name`" ]; }; " +
        "done; " +
        "mkdir '$remoteStage'; chmod 0700 '$remoteStage'"
    ) | Out-Null

    foreach ($artifact in $artifacts) {
        Invoke-Adb push $artifact.Path "$remoteStage/$($artifact.Name)" | Out-Null
    }
    Invoke-Adb shell (
        "set -eu; chown root:root '$remoteStage/'*; chmod 0755 '$remoteStage/'*; sync"
    ) | Out-Null

    foreach ($artifact in $artifacts) {
        $remoteHash = ((Invoke-Adb shell "sha256sum '$remoteStage/$($artifact.Name)'") -join "`n").Split(' ')[0]
        if ($remoteHash -ne $artifact.Sha256) {
            throw "Remote stage hash mismatch: $($artifact.Name)"
        }
    }

    # Keep a recoverable on-board copy before atomically replacing either executable.
    Invoke-Adb shell (
        "set -eu; " +
        "mkdir -p '$remoteBackup'; chmod 0700 '$remoteBackup'; " +
        "for name in gateway-ota-worker-v1 token-signer-daemon; do " +
        "if [ -e '$remoteBin/'`"`$name`" ]; then cp -p '$remoteBin/'`"`$name`" '$remoteBackup/'`"`$name`"; fi; " +
        "done; " +
        "start-stop-daemon -K -x '$remoteBin/token-signer-daemon' -o >/dev/null 2>&1 || true; " +
        "mv -f '$remoteStage/gateway-ota-worker-v1' '$remoteBin/gateway-ota-worker-v1'; " +
        "mv -f '$remoteStage/token-signer-daemon' '$remoteBin/token-signer-daemon'; " +
        "chown root:root '$remoteBin/gateway-ota-worker-v1' '$remoteBin/token-signer-daemon'; " +
        "chmod 0755 '$remoteBin/gateway-ota-worker-v1' '$remoteBin/token-signer-daemon'; sync"
    ) | Out-Null
    $published = $true

    foreach ($artifact in $artifacts) {
        $remoteHash = ((Invoke-Adb shell "sha256sum '$remoteBin/$($artifact.Name)'") -join "`n").Split(' ')[0]
        if ($remoteHash -ne $artifact.Sha256) {
            throw "Published hash mismatch: $($artifact.Name)"
        }
    }
}
catch {
    if (-not $published) {
        # A partial publish is rolled back only from the backup made in this invocation.
        Invoke-AdbResult shell (
            "set -eu; " +
            "for name in gateway-ota-worker-v1 token-signer-daemon; do " +
            "if [ -f '$remoteBackup/'`"`$name`" ] && [ ! -L '$remoteBackup/'`"`$name`" ]; then " +
            "rm -f '$remoteBin/'`"`$name`"; mv -f '$remoteBackup/'`"`$name`" '$remoteBin/'`"`$name`"; fi; " +
            "done; sync"
        ) | Out-Null
    }
    throw
}
finally {
    Invoke-AdbResult shell (
        "rm -f '$remoteStage/gateway-ota-worker-v1' '$remoteStage/token-signer-daemon'; " +
        "rmdir '$remoteStage' 2>/dev/null || true"
    ) | Out-Null
}

$artifacts | Format-Table Name, Size, Sha256 -AutoSize
Write-Output "Direct Gateway OTA programs published: $remoteBin"
Write-Output "Recoverable prior copies: $remoteBackup"
Write-Output 'No HawkBit, SWUpdate, or orchestrator process was started.'
