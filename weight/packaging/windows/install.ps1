<#
    Installs Weight for the current user.

    Deliberately per-user and registry-free where possible: it needs no
    administrator rights, and uninstalling is a matter of deleting a folder and
    two shortcuts. Nothing it does can touch the workspace folder, which lives
    under Documents and holds the person's records.

    Usage:
        powershell -ExecutionPolicy Bypass -File install.ps1
        powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
#>
param([switch]$Uninstall)

$ErrorActionPreference = 'Stop'

$AppName   = 'Weight'
$ExeName   = 'weight.exe'
$InstallTo = Join-Path $env:LOCALAPPDATA 'Programs\Weight'
$StartMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$Desktop   = [Environment]::GetFolderPath('Desktop')

function Add-ToUserPath([string]$Directory) {
    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($current -split ';' -contains $Directory) { return }
    $updated = if ([string]::IsNullOrEmpty($current)) { $Directory } else { "$current;$Directory" }
    [Environment]::SetEnvironmentVariable('Path', $updated, 'User')
    Write-Host "  Added to your PATH: $Directory"
}

function Remove-FromUserPath([string]$Directory) {
    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ([string]::IsNullOrEmpty($current)) { return }
    $kept = ($current -split ';' | Where-Object { $_ -ne $Directory }) -join ';'
    [Environment]::SetEnvironmentVariable('Path', $kept, 'User')
}

function New-Shortcut([string]$Path, [string]$Target) {
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($Path)
    $link.TargetPath = $Target
    $link.WorkingDirectory = Split-Path $Target
    $link.IconLocation = $Target
    $link.Description = 'Record body mass and plot trend curves'
    $link.Save()
}

if ($Uninstall) {
    Write-Host "Removing $AppName..."
    foreach ($shortcut in @((Join-Path $StartMenu "$AppName.lnk"),
                            (Join-Path $Desktop  "$AppName.lnk"))) {
        if (Test-Path $shortcut) { Remove-Item $shortcut -Force }
    }
    Remove-FromUserPath $InstallTo
    if (Test-Path $InstallTo) { Remove-Item $InstallTo -Recurse -Force }
    Write-Host ''
    Write-Host "Removed. Your measurements in Documents\weight were left untouched."
    exit 0
}

$source = Join-Path (Split-Path -Parent $PSScriptRoot) '..\target' | Resolve-Path -ErrorAction SilentlyContinue
if (-not $source) { $source = (Get-Location).Path }
$exe = Join-Path $source $ExeName
if (-not (Test-Path $exe)) {
    throw "$ExeName was not found in $source. Build it first."
}

Write-Host "Installing $AppName to $InstallTo"
New-Item -ItemType Directory -Force -Path $InstallTo | Out-Null
Copy-Item $exe (Join-Path $InstallTo $ExeName) -Force

# One binary with everything inside it: nothing else to copy, no DLLs.
Add-ToUserPath $InstallTo
New-Shortcut (Join-Path $StartMenu "$AppName.lnk") (Join-Path $InstallTo $ExeName)
New-Shortcut (Join-Path $Desktop  "$AppName.lnk") (Join-Path $InstallTo $ExeName)

Write-Host ''
Write-Host "Done. Open a new terminal and run:  weight"
Write-Host "$AppName is also in the Start menu and on the Desktop."
