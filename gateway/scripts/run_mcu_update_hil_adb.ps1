[CmdletBinding()]
param(
    [string]$AdbPath = 'E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe',
    [string]$Serial = '00675779d0c446e21d4',
    [Parameter(Mandatory = $true)][string]$SwuPath,
    [Parameter(Mandatory = $true)][string]$HawkBitStateFile,
    [string]$WslDistribution = 'Ubuntu-24.04',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-ShellLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value.Contains("'")) { throw 'Shell argument contains an unsupported quote.' }
    "'$Value'"
}

function Invoke-Adb {
    param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $output = & $AdbPath -s $Serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed: $($Arguments -join ' ')`n$($output -join "`n")"
    }
    @($output)
}

function Invoke-AdbShell {
    param([Parameter(Mandatory = $true)][string]$Command)

    Invoke-Adb shell $Command
}

function Invoke-WslScript {
    param([Parameter(Mandatory = $true)][string]$Arguments)

    $command = "cd /mnt/e/T527/can_boot/gateway; ./scripts/hawkbit_action_wsl.sh $Arguments"
    $output = & wsl.exe -d $WslDistribution -- bash -lc $command 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "hawkBit command failed:`n$($output -join "`n")"
    }
    @($output)
}

if (-not (Test-Path -LiteralPath $AdbPath -PathType Leaf)) { throw "adb executable does not exist: $AdbPath" }
if (-not (Test-Path -LiteralPath $SwuPath -PathType Leaf)) { throw "SWU artifact does not exist: $SwuPath" }
if ((Get-Item -LiteralPath $SwuPath -Force).LinkType) { throw 'SWU artifact must not be a symbolic link.' }
if ($Serial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'Device serial contains unsupported characters.' }
if ($HawkBitStateFile -notmatch '^/[A-Za-z0-9._/-]+$' -or $HawkBitStateFile.Contains('..') -or $HawkBitStateFile.Contains('//')) {
    throw 'HawkBitStateFile must be a normalized absolute WSL path.'
}

$swuFullPath = (Get-Item -LiteralPath $SwuPath).FullName
$swuWslPath = (& wsl.exe -d $WslDistribution -- wslpath -a $swuFullPath 2>&1 | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0 -or $swuWslPath -notmatch '^/mnt/[a-z]/') { throw 'Cannot resolve the SWU artifact inside WSL.' }
if (((Invoke-Adb get-state | Select-Object -Last 1).Trim()) -ne 'device') { throw "ADB device is not ready: $Serial" }

Invoke-AdbShell 'systemctl is-active --quiet mcu-update-token-signer.service mcu-updater.service mcu-update-swupdate.service' | Out-Null
Invoke-AdbShell 'test -S /run/mcu-update/remote-handler/mcu-v1' | Out-Null
$startEpoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()

$assignArguments = 'assign --artifact ' + (ConvertTo-ShellLiteral $swuWslPath) +
                   ' --state ' + (ConvertTo-ShellLiteral $HawkBitStateFile)
Invoke-WslScript $assignArguments | Out-Null

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$completed = $false
do {
    Start-Sleep -Seconds 2
    $journal = (Invoke-AdbShell "journalctl -u mcu-updater.service --since '@$startEpoch' --no-pager -o cat") -join "`n"
    $statusArguments = 'status --state ' + (ConvertTo-ShellLiteral $HawkBitStateFile)
    $action = (Invoke-WslScript $statusArguments) -join "`n"
    if ($journal -match 'outcome=FAILED|terminal failure') {
        throw "The production updater reported a terminal failure.`n$journal"
    }
    $completed = $journal -match 'outcome=CONFIRMED' -and
                 $action -match '(?m)^ACTION_STATUS=FINISHED$' -and
                 $action -match '(?m)^ACTION_ACTIVE=false$'
} while (-not $completed -and [DateTime]::UtcNow -lt $deadline)

if (-not $completed) {
    $service = (Invoke-AdbShell 'systemctl show mcu-updater.service -p ActiveState -p SubState -p Result -p ExecMainStatus') -join "`n"
    throw "Timed out waiting for a confirmed production update.`n$service`n$journal`n$action"
}

Invoke-AdbShell 'systemctl is-active --quiet mcu-updater.service' | Out-Null
Write-Output 'Production single-MCU update HIL passed.'
Write-Output $action
