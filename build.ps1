# build.ps1 -- the one documented command for Windows (Track C3; PowerShell 5.1+ / 7).
#
#   .\build.ps1 -Disc IMAGE      configure + build the windows-release preset (clang-cl, Ninja,
#                                SDL2 + FFmpeg from the vcpkg manifest), run the test suite, stage
#                                the artifact under dist\windows-x86_64\ (+ the zip). IMAGE is a
#                                disc image (.iso / raw .bin) of YOUR copy of the game: the build
#                                extracts the title's ELF and IOP module from it and recompiles
#                                them (the recompiled code is derived from the game and is not in
#                                this tree). Also: NATIVEEE_DISC=IMAGE in the environment.
#   .\build.ps1 -Elf SLUS_123.45 [-Irx STREAM.IRX]
#                                the same from loose files (NATIVEEE_TITLE_ELF / NATIVEEE_STREAM_IRX)
#   .\build.ps1 -NoDist          stop after the build; WITHOUT a disc/ELF only the engine, the
#                                recompilers and the component tests are built (the title is skipped)
#   .\build.ps1 -NoTests         skip ctest
#   .\build.ps1 -NoDist          stop after the build
#   .\build.ps1 -SkipBuild -Dist only (re)stage dist\windows-x86_64\ from an existing build
#   .\build.ps1 -Preset NAME     another preset (windows-dev, windows-msvc-dev)
#   .\build.ps1 -Jobs N          parallel compile jobs
#   .\build.ps1 -Help            this text
#
# Prerequisites (each one documented in README.md "Developers"):
#   - Visual Studio 2022 Build Tools with the "C++ Clang tools for Windows" component
#     (clang-cl, Ninja) -- run this script from a "Developer PowerShell for VS 2022", or let
#     it import the environment itself (it looks for vswhere / VsDevCmd.bat).
#   - vcpkg: a checkout with VCPKG_ROOT pointing at it (bootstrap-vcpkg.bat run once). The
#     dependencies come from vcpkg.json; the first configure builds them (minutes).
#   - CMake 3.21+ and Python 3.8+ on PATH.
#
# The artifact (README.md "Users"), the same layout as dist/macos and dist/linux-<arch>:
#   dist\windows-x86_64\nativeee.exe       the binary = recompiled code + native engine only
#   dist\windows-x86_64\*.dll               every DLL vcpkg's applocal deployment placed beside
#                                          the built exe: SDL2.dll, avcodec-*.dll, avutil-*.dll,
#                                          swscale-*.dll and their dependencies
#   dist\windows-x86_64\nativeee.pdb       the crash backtrace symbols (host_windows.cpp)
#   dist\windows-x86_64\title\              the title's own tables the binary reads at start
#                                          (hle\sdk_bindings.tsv, data\post_load_patches.txt,
#                                          verify\restore_mhash_expected.tsv -- no game bytes)
#   dist\windows-x86_64\BUILD-ID            the build id the binary prints with --version
#   dist\windows-x86_64\README.md           the user half of the repo README
#   dist\windows-x86_64\LICENSE             the project licence (MIT; the repository root LICENSE)
#   dist\windows-x86_64\THIRD_PARTY_NOTICES.md  the bundled libraries' notices
#   dist\windows-x86_64\licenses\           the bundled libraries' licence texts: the committed
#                                           tools\dist\licenses set (SDL2, FFmpeg LGPL + LICENSE.md +
#                                           the configure line libavcodec EMBEDS in the bundled DLL /
#                                           the exe -- refused on any GPL / non-free / version3 flag --,
#                                           Dear ImGui, the gl3w loader) + vcpkg's
#                                          share\<port>\copyright: sdl2, ffmpeg, ...)
#   dist\nativeee-windows-x86_64.zip       the one-file download of that folder
# The GL loader is opengl32.dll from Windows; nothing to bundle. Game data is NEVER part of
# it: tools/ci/asset_scan.py (names, format magic, byte windows of the title's ELF inside every
# file) runs on the staged folder and on the zip; the script refuses to finish on a finding.
[CmdletBinding()]
param(
    [string]$Preset = "windows-release",
    [string]$Disc = $env:NATIVEEE_DISC,
    [string]$Elf = $env:NATIVEEE_TITLE_ELF,
    [string]$Irx = $env:NATIVEEE_STREAM_IRX,
    [int]$Jobs = 0,
    [switch]$NoTests,
    [switch]$NoDist,
    [switch]$SkipBuild,
    [switch]$Dist,
    [switch]$Help
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root
if ($Help) {
    Get-Content $MyInvocation.MyCommand.Path | Select-Object -Skip 1 -First 50 | ForEach-Object { $_ -replace '^# ?', '' }
    exit 0
}

function Step($msg) { Write-Host ""; Write-Host "== $msg" -ForegroundColor Cyan }

# A native command whose stderr is merged (2>&1) terminates a Windows PowerShell 5.1 script
# under $ErrorActionPreference = "Stop" on its FIRST stderr line (a compiler warning, ninja's
# own notes) -- NativeCommandError. Run such commands with the preference relaxed and judge
# them by $LASTEXITCODE, exactly as build.sh judges `cmake ... | tee`.
function Invoke-Native([scriptblock]$Block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $global:LASTEXITCODE = 0
    # The block's own output goes to the host (so only the exit code is returned to the caller).
    try { & $Block | Out-Host } finally { $ErrorActionPreference = $saved }
    return $global:LASTEXITCODE
}

# --- the MSVC developer environment (clang-cl, Ninja, the Windows SDK on PATH) -----------
if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $devcmd = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $devcmd) {
            Step "importing the VS developer environment ($devcmd)"
            cmd /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
                if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") }
            }
        }
        # The "C++ Clang tools for Windows" component installs under VC\Tools\Llvm; VsDevCmd
        # does not always put it on PATH.
        $llvm = Join-Path $vs "VC\Tools\Llvm\x64\bin"
        if ($vs -and (Test-Path (Join-Path $llvm "clang-cl.exe")) -and -not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
            $env:PATH = "$llvm;$env:PATH"
        }
    }
}
foreach ($tool in @("cmake", "ninja", "clang-cl", "python")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "build.ps1: '$tool' not on PATH (see the prerequisites at the top of this script)"
    }
}
if (-not $env:VCPKG_ROOT -or -not (Test-Path (Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"))) {
    throw "build.ps1: VCPKG_ROOT must point at a vcpkg checkout (git clone https://github.com/microsoft/vcpkg; .\bootstrap-vcpkg.bat)"
}

# --- the preset's binaryDir, read from CMakePresets.json ---------------------------------
$presets = Get-Content (Join-Path $Root "CMakePresets.json") -Raw | ConvertFrom-Json
$cfg = $presets.configurePresets | Where-Object { $_.name -eq $Preset }
if (-not $cfg) { throw "build.ps1: no configure preset named '$Preset'" }
# binaryDir may be inherited (build.sh walks `inherits` the same way).
$walk = $cfg
while (-not $walk.binaryDir -and $walk.inherits) {
    $parent = if ($walk.inherits -is [string]) { $walk.inherits } else { $walk.inherits[0] }
    $walk = $presets.configurePresets | Where-Object { $_.name -eq $parent }
    if (-not $walk) { throw "build.ps1: preset '$Preset' inherits an unknown preset '$parent'" }
}
if (-not $walk.binaryDir) { throw "build.ps1: preset '$Preset' has no binaryDir" }
$BuildDir = $walk.binaryDir.Replace('${sourceDir}', $Root)
$Platform = "windows-x86_64"
$DistDir = Join-Path $Root "dist\$Platform"
$Zip = Join-Path $Root "dist\nativeee-$Platform.zip"

if (-not $SkipBuild) {
    # --- the title's inputs (cmake/TitleInputs.cmake): the artifact IS the title binary, whose
    #     recompiled code is generated from the game's own files -- staging dist\ needs a disc image
    #     or the ELF; without one only -NoDist (engine + recompilers + component tests) makes sense.
    $titleArgs = @()
    foreach ($pair in @(@("Disc", "NATIVEEE_DISC", $Disc), @("Elf", "NATIVEEE_TITLE_ELF", $Elf), @("Irx", "NATIVEEE_STREAM_IRX", $Irx))) {
        $name, $var, $val = $pair
        if (-not $val) { continue }
        if (-not (Test-Path -LiteralPath $val -PathType Leaf)) { throw "build.ps1: -$name ${val}: no such file" }
        $titleArgs += "-D$var=$((Resolve-Path -LiteralPath $val).Path)"
    }
    if (-not $NoDist -and -not $Disc -and -not $Elf) {
        throw ("build.ps1: no disc image / title ELF given -- the dist artifact is the title binary (nativeee.exe), whose " +
               "recompiled code is generated from YOUR copy of the game and is not part of this tree. " +
               ".\build.ps1 -Disc 'X:\path\the title.iso' (a .iso or raw .bin of the NTSC-U disc, SLUS-12345), or " +
               ".\build.ps1 -Elf X:\path\SLUS_123.45 [-Irx X:\path\STREAM.IRX], or set NATIVEEE_DISC / NATIVEEE_TITLE_ELF. " +
               "To build only the engine, the recompilers and the component tests (no title, no artifact): .\build.ps1 -NoDist")
    }
    Step "configure  (cmake --preset $Preset $($titleArgs -join ' '))"
    $rc = Invoke-Native { cmake --preset $Preset @titleArgs 2>&1 | ForEach-Object { "$_" } }
    if ($rc -ne 0) { throw "build.ps1: configure failed" }

    # Without the test run only the artifact's binary is built (the test executables are two
    # thirds of the target set; the CI artifact job and a user's build take this path) -- as
    # build.sh does.
    $buildArgs = @("--build", "--preset", $Preset)
    if ($Jobs -gt 0) { $buildArgs += @("-j", "$Jobs") }
    if ($NoTests) { $buildArgs += @("--target", "nativeee") }
    Step "build      (cmake $($buildArgs -join ' '))"
    # Acceptance shape: 0 FAILED, 0 compiler warnings, 0 linker warnings (the repo rule).
    $log = Join-Path $BuildDir "build.ps1.log"
    $rc = Invoke-Native { cmake @buildArgs 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log }
    if ($rc -ne 0) { throw "build.ps1: BUILD FAILED (log: $log)" }
    $nWarn = (Select-String -Path $log -Pattern "warning:" -SimpleMatch | Measure-Object).Count
    $nLdWarn = (Select-String -Path $log -Pattern "lld-link: warning" -SimpleMatch | Measure-Object).Count
    Write-Host "build.ps1: build OK -- warning: $nWarn, lld-link: warning $nLdWarn"

    if (-not $NoTests) {
        Step "tests      (component + title suites; the game-file groups Root*/Og* are excluded)"
        $rc = Invoke-Native { ctest --preset $Preset -E "^Root|^Og|Oracle" --timeout 900 -j4 2>&1 | ForEach-Object { "$_" } }
        if ($rc -ne 0) { throw "build.ps1: TESTS FAILED (ctest --preset $Preset --rerun-failed --output-on-failure)" }
    }
}

if ($NoDist -and -not $Dist) { exit 0 }

$exe = Join-Path $BuildDir "app\host\nativeee.exe"
if (-not (Test-Path $exe)) { throw "build.ps1: no binary at $exe (build first)" }

Step "stage      ($DistDir)"
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
if (Test-Path $Zip) { Remove-Item -Force $Zip }   # never leave a previous run's zip beside a failed stage
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item $exe (Join-Path $DistDir "nativeee.exe")
# vcpkg's applocal step (VCPKG_APPLOCAL_DEPS, on by default) copies every runtime DLL the exe
# needs next to it at link time; without them the exe is not self-contained (build.sh's
# "NOT self-contained" check for Linux).
$dllsBesideExe = @(Get-ChildItem (Split-Path $exe) -Filter "*.dll")
if (-not ($dllsBesideExe | Where-Object { $_.Name -like "SDL2*.dll" })) {
    throw "build.ps1: NOT self-contained -- no SDL2*.dll beside $exe (vcpkg applocal deployment did not run; configure with the vcpkg toolchain, VCPKG_APPLOCAL_DEPS=ON)"
}
$dllsBesideExe | ForEach-Object { Copy-Item $_.FullName $DistDir }
$pdb = [IO.Path]::ChangeExtension($exe, ".pdb")
if (Test-Path $pdb) { Copy-Item $pdb $DistDir }   # the crash backtrace symbols (host_windows.cpp)

# The title tables the binary reads beside itself when the source tree is absent (main.cpp:
# PS2R_TITLE_DIR, else <exe dir>\title).
# No serial is hardcoded: PS2R_TITLE if set, else the one title directory in games/ (one with
# an hle\ subdirectory). With no title there are no tables to stage.
$titleSrc = $null
if ($env:PS2R_TITLE) {
    $titleSrc = Join-Path $Root "games\$($env:PS2R_TITLE)"
} else {
    $found = @(Get-ChildItem (Join-Path $Root "games") -Directory -ErrorAction SilentlyContinue |
               Where-Object { Test-Path (Join-Path $_.FullName "hle") })
    if ($found.Count -gt 1) { throw "build.ps1: several titles in games/; set PS2R_TITLE to choose one" }
    if ($found.Count -eq 1) { $titleSrc = $found[0].FullName }
}
if ($titleSrc) {
    foreach ($rel in @("hle\sdk_bindings.tsv", "data\post_load_patches.txt", "verify\restore_mhash_expected.tsv")) {
        $src = Join-Path $titleSrc $rel
        if (-not (Test-Path $src)) { continue }
        $dst = Join-Path (Join-Path $DistDir "title") $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item $src $dst
    }
} else {
    Write-Output "build.ps1: no title in games/ -- dist\ carries no title tables"
}

$exeOut = Join-Path $DistDir "nativeee.exe"
$rc = Invoke-Native { & $exeOut --version 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding ascii (Join-Path $DistDir "BUILD-ID") }
if ($rc -ne 0) { throw "build.ps1: $exeOut --version failed (exit $rc) -- a DLL missing beside it?" }
Get-Content (Join-Path $DistDir "BUILD-ID")

# The user half of README.md: from the top to the "## Developers" heading.
$readme = Get-Content (Join-Path $Root "README.md")
$cut = ($readme | Select-String -Pattern '^## Developers' | Select-Object -First 1).LineNumber
if ($cut -gt 1) { $readme[0..($cut - 2)] | Out-File -Encoding utf8 (Join-Path $DistDir "README.md") }
else { $readme | Out-File -Encoding utf8 (Join-Path $DistDir "README.md") }
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $DistDir "LICENSE")   # MIT (the repository root LICENSE)

$dlls = Get-ChildItem $DistDir -Filter "*.dll" | ForEach-Object { "- $($_.Name)" }
# The bundled FFmpeg must be the LGPL build (vcpkg's ffmpeg port with its default features has no
# gpl / nonfree feature). The proof is read from WHAT WAS LINKED: the configuration string libavcodec
# embeds (avcodec_configuration() = the configure command line as run) in the bundled avcodec DLL,
# or -- a static vcpkg triplet -- in nativeee.exe itself. A string with --enable-gpl / -nonfree /
# -libx26x / -version3 is refused (it would put the whole artifact under the GPL, or under LGPLv3
# while licenses\ ships the v2.1 text), and so is a binary with NO such string (nothing to attest).
$avcodec = Get-ChildItem $DistDir -Filter "avcodec*.dll" | Select-Object -First 1
$ffObject = if ($avcodec) { $avcodec } else { Get-Item (Join-Path $DistDir "nativeee.exe") }
$ffConfFrom = if ($avcodec) { "$($avcodec.Name) (bundled DLL)" } else { "nativeee.exe (linked statically)" }
$ffConf = ""
$bytes = [System.IO.File]::ReadAllBytes($ffObject.FullName)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$confs = @([regex]::Matches($text, "--prefix=[\x20-\x7e]{0,4000}") | ForEach-Object { $_.Value } | Sort-Object -Unique)
if ($confs.Count -ne 1) {
    Remove-Item -Recurse -Force $DistDir
    throw "build.ps1: REFUSING to ship -- $($ffObject.Name) embeds $($confs.Count) FFmpeg configuration strings (--prefix=), expected exactly 1: $($confs -join ' | ')"
}
$ffConf = $confs[0]
if ($ffConf -match "--enable-gpl|--enable-nonfree|--enable-libx26|--enable-version3") {
    Remove-Item -Recurse -Force $DistDir
    throw "build.ps1: REFUSING to ship -- the FFmpeg linked into $ffConfFrom is a GPL/non-free/LGPLv3 build: $ffConf"
}
Write-Host "ffmpeg: configuration string read from $ffConfFrom (no GPL / non-free / version3 flag)"
$buildId = Get-Content (Join-Path $DistDir "BUILD-ID") -Raw
@(
    "# NativeEE -- third-party notices -- $($buildId.Trim())",
    "",
    "This folder ships the recompiled game program and the native engine. It contains NO game",
    "data: no textures, audio, models, FMVs or disc bytes (tools/ci/asset_scan.py runs on every",
    "artifact). You must own the title (SLUS-12345) and supply your own disc image.",
    "",
    "The licence texts named below are in licenses\ beside this file.",
    "",
    "## Bundled dynamic libraries (the DLLs beside nativeee.exe)",
    "",
    $dlls,
    "",
    "- **SDL2** -- zlib licence (licenses\SDL2-LICENSE.txt); vcpkg's build, dynamically linked.",
    "- **FFmpeg** avcodec, avutil, swscale -- GNU Lesser General Public License v2.1 or later",
    "  (licenses\FFmpeg-COPYING.LGPLv2.1.txt; per-component terms in licenses\FFmpeg-LICENSE.md),",
    "  as built by vcpkg (features avcodec, swscale; default features off; NO gpl / nonfree feature:",
    "  the DLL's configure line is in licenses\FFmpeg-BUILD-CONFIGURATION.txt and build.ps1 refuses",
    "  a GPL build). Dynamically linked and replaceable: swap the DLLs for another LGPL build of the",
    "  same major versions.",
    "- Other DLLs are those libraries' own dependencies, each under its own licence as distributed",
    "  by vcpkg (licenses\<port>-copyright.txt).",
    "",
    "## Compiled into the binary",
    "",
    "- **Dear ImGui** (the F1 developer overlay and the first-run launcher) -- MIT licence",
    "  (licenses\imgui-LICENSE.txt); its stb headers carry their own public-domain / MIT notices.",
    "- **OpenGL loader**: the engine links opengl32 and calls the exported entry points; the only",
    "  loader code in the binary is Dear ImGui's embedded gl3w-derived header, public domain",
    "  (licenses\imgl3w-UNLICENSE.txt).",
    "",
    "## From your system at run time (nothing bundled)",
    "",
    "opengl32.dll (the vendor's OpenGL driver), the Visual C++ runtime / UCRT.",
    "",
    "## The port's own code",
    "",
    "See LICENSE (the project licence)."
) | Out-File -Encoding utf8 (Join-Path $DistDir "THIRD_PARTY_NOTICES.md")

# The bundled libraries' licence texts: the committed set (tools\dist\licenses, pinned by
# tests\lint\lint_artifact_licenses.py) plus vcpkg's per-port copyright files.
$licDir = Join-Path $DistDir "licenses"
New-Item -ItemType Directory -Force -Path $licDir | Out-Null
$licSrc = Join-Path $Root "tools\dist\licenses"
foreach ($f in @("SDL2-LICENSE.txt", "FFmpeg-LICENSE.md", "FFmpeg-COPYING.LGPLv2.1.txt", "imgui-LICENSE.txt", "imgl3w-UNLICENSE.txt")) {
    Copy-Item (Join-Path $licSrc $f) (Join-Path $licDir $f)
}
# Generated from WHAT WAS LINKED (the string read above), never from a recipe or a cache;
# tests\lint\lint_artifact_licenses.py --dist re-reads the object and refuses a mismatch.
@("FFmpeg as linked into $ffConfFrom (vcpkg's build).",
  "",
  "libavcodec's configuration string embedded in $($ffObject.Name) (avcodec_configuration(); the configure",
  "command line the linked FFmpeg was really built with; read by build.ps1 from the file's bytes):",
  $ffConf,
  "",
  "No GPL / non-free / version3 flag (checked by build.ps1; vcpkg features avcodec, swscale, default features off).") |
    Out-File -Encoding utf8 (Join-Path $licDir "FFmpeg-BUILD-CONFIGURATION.txt")
$installed = Join-Path $BuildDir "vcpkg_installed"
if (Test-Path $installed) {
    Get-ChildItem -Path $installed -Recurse -Filter "copyright" -File | ForEach-Object {
        $port = $_.Directory.Name
        Copy-Item $_.FullName (Join-Path $licDir "$port-copyright.txt")
    }
}
python (Join-Path $Root "tests\lint\lint_artifact_licenses.py") --root $Root --dist $DistDir
if ($LASTEXITCODE -ne 0) { Remove-Item -Recurse -Force $DistDir; throw "build.ps1: REFUSING to ship -- the licence set in $DistDir is incomplete or inconsistent" }

# The asset guard: the C1 asset scan (names, format magic, ELF byte windows) on the staged folder.
Step "asset scan ($DistDir)"
$rc = Invoke-Native { python (Join-Path $Root "tools\ci\asset_scan.py") $DistDir --report (Join-Path $BuildDir "asset_scan_dist.md") 2>&1 | ForEach-Object { "$_" } }
if ($rc -ne 0) { Remove-Item -Recurse -Force $DistDir; throw "build.ps1: REFUSING to ship -- the asset scan found game data in $DistDir (report: $BuildDir\asset_scan_dist.md)" }

# The one-file download, scanned as well.
# (Windows PowerShell 5.1's Compress-Archive writes backslash entry names; PowerShell 7 -- the
# CI shell -- writes portable ones. Use pwsh for a zip meant for other tools.)
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $DistDir -DestinationPath $Zip
$rc = Invoke-Native { python (Join-Path $Root "tools\ci\asset_scan.py") $Zip --report (Join-Path $BuildDir "asset_scan_archive.md") 2>&1 | ForEach-Object { "$_" } }
if ($rc -ne 0) { Remove-Item -Force $Zip; throw "build.ps1: REFUSING to ship -- the asset scan found game data in $Zip" }

Step "artifact   ($DistDir)"
Get-ChildItem $DistDir | Format-Table Name, Length -AutoSize
Write-Host "zip: $Zip ($((Get-Item $Zip).Length) bytes, sha256 $((Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLower()))"
Write-Host "build.ps1: done -- $DistDir\nativeee.exe ($(Get-Content (Join-Path $DistDir 'BUILD-ID')))"
