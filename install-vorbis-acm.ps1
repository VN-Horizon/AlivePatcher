#Requires -Version 5.0
<#
.SYNOPSIS
  Install AlivePatcher system vorbis.acm (manual / standalone).
  Runtime path uses the copy embedded in winmm.dll.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Source,
    [string]$RestartExe = ''
)

$ErrorActionPreference = 'Stop'
$log = Join-Path $env:TEMP 'alive_vorbis_install.log'
function Log([string]$m) {
    Add-Content -LiteralPath $log -Value (("$(Get-Date -Format o) ") + $m) -Encoding UTF8
    Write-Host $m
}

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdmin)) {
    $self = $MyInvocation.MyCommand.Path
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $self, '-Source', $Source)
    if ($RestartExe) { $argList += @('-RestartExe', $RestartExe) }
    $p = Start-Process -FilePath powershell.exe -Verb RunAs -ArgumentList $argList -PassThru -Wait
    exit $p.ExitCode
}

Log "Source=$Source"
if (-not (Test-Path -LiteralPath $Source)) {
    Log 'ERROR source missing'
    exit 1
}

$win = $env:SystemRoot
$wowDst = Join-Path $win 'SysWOW64\vorbis.acm'
$sysNative = Join-Path $win 'Sysnative\vorbis.acm'
$sysDst = Join-Path $win 'System32\vorbis.acm'

Copy-Item -LiteralPath $Source -Destination $wowDst -Force
Log "copied SysWOW64: $wowDst exist=$(Test-Path -LiteralPath $wowDst)"

if (Test-Path -LiteralPath (Join-Path $win 'Sysnative')) {
    Copy-Item -LiteralPath $Source -Destination $sysNative -Force
    Log "copied System32 via Sysnative: exist=$(Test-Path -LiteralPath $sysNative)"
} else {
    Copy-Item -LiteralPath $Source -Destination $sysDst -Force
    Log "copied System32: exist=$(Test-Path -LiteralPath $sysDst)"
}

function Set-Drivers32([Microsoft.Win32.RegistryView]$view) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    $d32 = $base.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32', $true)
    if (-not $d32) { throw "Drivers32 missing ($view)" }
    $d32.SetValue('msacm.vorbis', 'vorbis.acm', [Microsoft.Win32.RegistryValueKind]::String)
    $d32.Close()
    $desc = $base.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers.desc', $true)
    if (-not $desc) {
        $desc = $base.CreateSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers.desc')
    }
    $desc.SetValue('vorbis.acm', 'Ogg Vorbis Audio CODEC', [Microsoft.Win32.RegistryValueKind]::String)
    $desc.Close()
    $base.Close()
    Log "registry ok view=$view"
}

Set-Drivers32 ([Microsoft.Win32.RegistryView]::Registry32)
Set-Drivers32 ([Microsoft.Win32.RegistryView]::Registry64)

Log 'install ok'
if ($RestartExe -and (Test-Path -LiteralPath $RestartExe)) {
    $wd = Split-Path -Parent $RestartExe
    Log "restart $RestartExe"
    Start-Process -FilePath $RestartExe -WorkingDirectory $wd
}
exit 0
