# tools/ci/release_launcher.ps1 -- package the engine's launcher (app/launcher, Qt 6 Widgets) for a TITLE on Windows:
# a portable folder (the .exe with the Qt DLLs beside it -- windeployqt, dynamic, LGPL -- the manifest under titles\,
# BUILD-ID, THIRD_PARTY_NOTICES.md, licenses\) and its zip. No installer: unzip and run. The Windows twin of
# tools/ci/release_launcher.sh. Title-neutral: everything about the game comes from -Manifest (the title's
# launcher\title.json).
# [From TwinsanityEvolved/tools/ci/release_launcher.ps1, made generic (manifest-driven, in the engine) and told the
#  D-A story: the launcher downloads the engine bundle + the compiler kit of its own release and prepares the game on
#  the player's machine.]
#
#   powershell -ExecutionPolicy Bypass -File tools\ci\release_launcher.ps1 -Manifest ..\launcher\title.json
#              -Qt C:\Qt\6.8.3\msvc2022_64 [-Engine DIR] [-BuildDir DIR] [-DistDir DIR] [-Version TAG] [-SkipBuild] [-Stub]
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Manifest,
    [string]$Qt = $env:QT_PREFIX,
    [string]$Engine = "",
    [string]$BuildDir = "",
    [string]$DistDir = "",
    [string]$Version = "",
    [string]$Licence = "",
    [switch]$SkipBuild,
    [switch]$Stub
)
$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Engine) { $Engine = (Resolve-Path (Join-Path $Here "..\..")).Path } else { $Engine = (Resolve-Path $Engine).Path }
if (-not $BuildDir) { $BuildDir = Join-Path $Engine "build-launcher-windows" }
if (-not $DistDir) { $DistDir = Join-Path $Engine "dist" }
New-Item -ItemType Directory -Force -Path $BuildDir, $DistDir | Out-Null
$BuildDir = (Resolve-Path $BuildDir).Path; $DistDir = (Resolve-Path $DistDir).Path
$Manifest = (Resolve-Path $Manifest).Path
function Step($t) { Write-Host ""; Write-Host "=== $t" }
$m = Get-Content $Manifest -Raw | ConvertFrom-Json
$Product = $m.product; $TitleId = $m.id
if (-not $Product -or -not $TitleId) { throw "release_launcher: the manifest needs product and id" }
$LauncherName = if ($m.PSObject.Properties['launcher_name'] -and $m.launcher_name) { $m.launcher_name } elseif ($m.app_data_folder) { "$($m.app_data_folder) Launcher" } else { "$Product Launcher" }
$ProductLc = $Product.ToLower()
if (-not $Version) { $Version = (git -C $Engine describe --tags --always 2>$null); if (-not $Version) { $Version = (git -C $Engine rev-parse --short HEAD) } }
$Platform = "windows-x64"
Write-Host "product $Product ($TitleId)  launcher name '$LauncherName'  version $Version  platform $Platform"
if (-not $Licence) { $Licence = Join-Path (Split-Path -Parent (Split-Path -Parent $Manifest)) "LICENSE" }
if (-not (Test-Path $Licence)) { $Licence = Join-Path $Engine "LICENSE" }

$ExeName = "ps2r_launcher"
$QtVersion = "none (stub)"
if (-not $Stub) {
    if (-not (Test-Path (Join-Path $Engine "app\launcher\CMakeLists.txt"))) { throw "release_launcher: $Engine\app\launcher is not there; -Stub for a pipeline dry run" }
    if (-not $Qt) { throw "release_launcher: -Qt PREFIX (the Qt 6 kit, e.g. C:\Qt\6.8.3\msvc2022_64)" }
    $windeployqt = Join-Path $Qt "bin\windeployqt.exe"
    if (-not (Test-Path $windeployqt)) { throw "release_launcher: no $windeployqt" }
    $QtVersion = & (Join-Path $Qt "bin\qmake.exe") -query QT_VERSION
    if (-not $SkipBuild) {
        Step "build the launcher ($Engine\app\launcher -> $BuildDir; Qt $QtVersion)"
        cmake -S (Join-Path $Engine "app\launcher") -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$Qt" "-DPS2R_LAUNCHER_VERSION=$Version"
        if ($LASTEXITCODE -ne 0) { throw "configure failed" }
        cmake --build $BuildDir
        if ($LASTEXITCODE -ne 0) { throw "build failed" }
        Step "the launcher's tests (QT_QPA_PLATFORM=offscreen)"
        $env:PATH = "$(Join-Path $Qt 'bin');$env:PATH"   # the test executables load the kit's DLLs
        $env:QT_QPA_PLATFORM = "offscreen"
        ctest --test-dir $BuildDir --output-on-failure
        $rc = $LASTEXITCODE
        Remove-Item Env:QT_QPA_PLATFORM
        if ($rc -ne 0) { throw "release_launcher: the launcher's tests failed" }
    }
}

$Dist = Join-Path $DistDir "launcher-$Platform"
$Folder = Join-Path $Dist "$LauncherName"   # the folder inside the zip players see
Step "stage $Folder"
if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
New-Item -ItemType Directory -Force -Path (Join-Path $Folder "titles"), (Join-Path $Folder "licenses") | Out-Null
if ($Stub) {
    "@echo off`r`necho STUB launcher -- a pipeline dry run placeholder`r`ntype `"%~dp0BUILD-ID`"`r`n" | Set-Content (Join-Path $Folder "$LauncherName.cmd") -Encoding ASCII
} else {
    $Exe = Get-ChildItem $BuildDir -Recurse -Filter "$ExeName.exe" | Select-Object -First 1
    if (-not $Exe) { throw "release_launcher: no $ExeName.exe under $BuildDir" }
    Copy-Item $Exe.FullName (Join-Path $Folder "$LauncherName.exe")
}
Copy-Item $Manifest (Join-Path $Folder "titles\title.json")
$stubNote = if ($Stub) { " STUB -- not a launcher build; a pipeline dry run" } else { "" }
@(
    "$LauncherName $Version ($Platform)$stubNote",
    "title   $TitleId ($Product)",
    "engine  $(git -C $Engine rev-parse HEAD 2>$null)",
    "qt      $QtVersion",
    "built   $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
) | Set-Content (Join-Path $Folder "BUILD-ID") -Encoding ASCII
Get-Content (Join-Path $Folder "BUILD-ID")
Copy-Item $Licence (Join-Path $Folder "LICENSE")
$lgpl = $null
if ($Qt) { $lgpl = Get-ChildItem -Path $Qt -Recurse -Filter "LICENSE.LGPL3" -ErrorAction SilentlyContinue | Select-Object -First 1 }
if (-not $lgpl) { $lgpl = Get-Item (Join-Path $Engine "tools\dist\licenses\Qt-LICENSE.LGPL3.txt") -ErrorAction SilentlyContinue }
if (-not $lgpl) { $lgpl = Get-Item (Join-Path $Here "..\dist\licenses\Qt-LICENSE.LGPL3.txt") }
Copy-Item $lgpl.FullName (Join-Path $Folder "licenses\Qt-LICENSE.LGPL3")
@"
# $LauncherName -- third-party notices -- $LauncherName $Version ($Platform)

This application is the NativeEE engine's launcher (app/launcher, MIT) carrying this title's manifest. It contains
NO game data and no code derived from the game. When you press Play the first time it downloads, from the same
release it came from, the engine (prebuilt; no game code in it) and a compiler, reads YOUR disc image, and prepares
the game on this computer. Nothing made from your disc is ever uploaded or shared.

| component | how it is linked | licence | text |
|---|---|---|---|
| $Product (the manifest, this packaging) | titles\title.json | the title repository's | LICENSE |
| NativeEE launcher (app/launcher) | the executable | MIT | the engine repository's LICENSE |
| Qt $QtVersion (Qt6Core, Qt6Gui, Qt6Widgets, Qt6Network, Qt6Concurrent, the platform plugin) | DYNAMIC: the DLLs beside the executable | LGPL v3 | licenses\Qt-LICENSE.LGPL3 |

Qt is used under the LGPL v3: it is linked dynamically and may be replaced by another build of the same major
version (section 4(d)). Qt's source: https://download.qt.io/official_releases/qt/ ; the launcher's: the engine repository.
"@ | Set-Content (Join-Path $Folder "THIRD_PARTY_NOTICES.md") -Encoding UTF8

if (-not $Stub) {
    Step "windeployqt (the Qt DLLs and plugins beside the executable, dynamic)"
    & $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $Folder "$LauncherName.exe")
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
}

Step "asset scan over the staged folder"
& python (Join-Path $Engine "tools\ci\asset_scan.py") $Dist --report (Join-Path $DistDir "asset_scan_launcher-$Platform.md")
if ($LASTEXITCODE -ne 0) { throw "release_launcher: the asset scan found something in $Dist" }

Step "zip"
$ZipName = "$ProductLc-launcher-$Version-$Platform.zip"
$Zip = Join-Path $DistDir $ZipName
if (Test-Path $Zip) { Remove-Item $Zip }
Compress-Archive -Path $Folder -DestinationPath $Zip -CompressionLevel Optimal
$sha = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLower()
"$sha  $ZipName" | Set-Content "$Zip.sha256" -Encoding ASCII
Write-Host "zip: $Zip ($([math]::Round((Get-Item $Zip).Length / 1MB)) MB)  sha256 $sha"
if ($Stub) { "STUB -- not a launcher build; a pipeline dry run" | Set-Content "$Zip.STUB" -Encoding ASCII }   # release_index.py refuses it
Write-Host ""; Write-Host "release_launcher: OK -> $Folder$(if ($Stub) { ' (STUB)' })"
