#Requires -Version 5.0
<#
.SYNOPSIS
  Uninstall AlivePatcher system vorbis.acm (files + Drivers32 registry).
#>
$ErrorActionPreference = 'Stop'

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdmin)) {
    Write-Host 'Elevating to Administrator...'
    $self = $MyInvocation.MyCommand.Path
    Start-Process powershell.exe -Verb RunAs -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$self`""
    ) -Wait
    exit $LASTEXITCODE
}

$removed = @()
$failed = @()

function Remove-FileSafe([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "skip (missing): $path"
        return
    }
    try {
        Remove-Item -LiteralPath $path -Force
        Write-Host "removed file: $path"
        $script:removed += $path
    } catch {
        Write-Host "FAILED file: $path — $($_.Exception.Message)"
        $script:failed += $path
    }
}

function Remove-RegValue([string]$keyPath, [string]$name) {
    if (-not (Test-Path -LiteralPath $keyPath)) {
        Write-Host "skip (key missing): $keyPath\$name"
        return
    }
    $props = Get-ItemProperty -LiteralPath $keyPath -ErrorAction SilentlyContinue
    if (-not $props -or -not ($props.PSObject.Properties.Name -contains $name)) {
        Write-Host "skip (value missing): $keyPath\$name"
        return
    }
    try {
        Remove-ItemProperty -LiteralPath $keyPath -Name $name -Force
        Write-Host "removed reg: $keyPath\$name"
        $script:removed += "$keyPath\$name"
    } catch {
        Write-Host "FAILED reg: $keyPath\$name — $($_.Exception.Message)"
        $script:failed += "$keyPath\$name"
    }
}

Write-Host '=== AlivePatcher: uninstall vorbis.acm ==='

Remove-FileSafe (Join-Path $env:SystemRoot 'SysWOW64\vorbis.acm')
Remove-FileSafe (Join-Path $env:SystemRoot 'System32\vorbis.acm')

$drivers32 = @(
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers32',
    'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32'
)
$driversDesc = @(
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers.desc',
    'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers.desc'
)

foreach ($k in $drivers32) { Remove-RegValue $k 'msacm.vorbis' }
foreach ($k in $driversDesc) { Remove-RegValue $k 'vorbis.acm' }

Write-Host ''
Write-Host "done. removed=$($removed.Count) failed=$($failed.Count)"
if ($failed.Count -gt 0) {
    Write-Host 'Close any app using vorbis.acm (e.g. Alive), then re-run.'
    exit 1
}
exit 0
