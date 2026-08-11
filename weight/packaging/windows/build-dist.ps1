<#
    Collects a redistributable folder for Windows into target\packages, and a
    .zip of the same folder.

    What this produces
    ------------------
        target\packages\weight-<version>-windows-<arch>\
            weight.exe
            Qt6Core.dll, Qt6Gui.dll, ...        (placed by windeployqt)
            platforms\qwindows.dll              (dlopen'd; nothing links it)
            styles\, imageformats\, ...
            install.ps1
            doc\
        target\packages\weight-<version>-windows-<arch>.zip

    Why a folder and not an installer
    ---------------------------------
    The brief asks for a binary, not an MSI. A folder that can be copied
    anywhere and run is exactly that, and it is also what the .zip contains, so
    there is one artefact to verify rather than two.

    Why the DLLs are here at all
    ----------------------------
    A single .exe with no DLLs beside it requires a statically linked Qt, which
    the official Qt binaries are not. Rather than shipping something that only
    works on the machine that built it, this collects the Qt runtime next to
    the executable — which is what every Qt application on Windows does — and
    says so.

    Usage (normally through CMake):
        cmake --build build --target dist
#>
$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$version    = (Get-Content (Join-Path $root 'VERSION')).Trim()
$buildDir   = if ($env:WEIGHT_BUILD_DIR)   { $env:WEIGHT_BUILD_DIR }   else { Join-Path $root 'build' }
$targetDir  = if ($env:WEIGHT_TARGET_DIR)  { $env:WEIGHT_TARGET_DIR }  else { Join-Path $root 'target' }
$stageRoot  = if ($env:WEIGHT_STAGING_DIR) { $env:WEIGHT_STAGING_DIR } else { Join-Path $buildDir 'package-staging' }

$exe = Join-Path $targetDir 'weight.exe'
if (-not (Test-Path $exe)) {
    throw "weight.exe was not found at $exe. Build it first: cmake --build build"
}

# The architecture of the artefact, taken from the PE header rather than from
# the host, because a cross-compiled arm64 build on an amd64 machine must not
# be labelled amd64.
$arch = 'x64'
try {
    $bytes = [System.IO.File]::ReadAllBytes($exe)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine  = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    switch ($machine) {
        0x8664  { $arch = 'x64' }
        0xAA64  { $arch = 'arm64' }
        0x014c  { $arch = 'x86' }
        default { $arch = "machine-$('{0:X4}' -f $machine)" }
    }
} catch {
    Write-Warning "Could not read the PE header; labelling the package '$arch'."
}

$name    = "weight-$version-windows-$arch"
$stage   = Join-Path $stageRoot "windows\$name"
$outDir  = Join-Path $targetDir 'packages'

Write-Host "==> Staging into $stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

# `cmake --install` runs the same install rules the package would, so the
# folder cannot drift from what `make install` produces. The install rule for
# Windows runs install.ps1 at the end; DESTDIR-style staging is expressed with
# --prefix, and WEIGHT_SKIP_USER_INSTALL keeps that step from touching the
# machine doing the packaging.
$env:WEIGHT_SKIP_USER_INSTALL = '1'
& cmake --install $buildDir --prefix $stage | Out-Null
Remove-Item Env:\WEIGHT_SKIP_USER_INSTALL

if (-not (Test-Path (Join-Path $stage 'weight.exe'))) {
    throw "The staged folder has no weight.exe; the install rules did not run."
}
if (-not (Test-Path (Join-Path $stage 'platforms'))) {
    Write-Warning @'
No platforms\ directory was staged, which means windeployqt did not run.
The program will fail at startup with "could not load the Qt platform plugin".
Put windeployqt on PATH (it lives in the Qt bin directory) and reconfigure.
'@
}

Write-Host "==> Copying to $outDir"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$final = Join-Path $outDir $name
if (Test-Path $final) { Remove-Item $final -Recurse -Force }
Copy-Item $stage $final -Recurse

$zip = "$final.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $final -DestinationPath $zip

Write-Host ''
Write-Host "==> Folder written:  $final"
Write-Host "==> Archive written: $zip"
Write-Host '==> Nothing was installed. Run install.ps1 inside the folder to add'
Write-Host '    the weight command, the Start-menu entry and the desktop shortcut.'
