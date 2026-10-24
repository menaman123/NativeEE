# tools/ci/release_dist.ps1 -- the Windows arm of the release workflow and its local reproduction (PowerShell 5.1+ / 7).
# [TwinsanityEvolved/tools/ci/release_dist.ps1 (234718f) with this tree's shape: no wasm32 toolchain to fetch (the title
#  is a static recompilation regenerated from the user's disc at build time), the artefact is nativeee.exe + gs_replay.exe
#  + the title's tables, the title directory comes from -GamesDir / -Disc / -Elf as build.ps1 takes them.]
#
#   .\tools\ci\release_dist.ps1 -Disc "X:\path\disc.iso" [-GamesDir DIR] [-Preset windows-clang-cl-release] [-BuildDir DIR]
#                               [-DistDir DIR] [-Jobs N] [-SkipBuild] [-NoZip] [-Lto]
#
# What it does, in order (the Windows counterpart of the engine's build.ps1 staging, in the port's release-workflow shape):
#   1. configure + build through the Windows CMake PRESET (CMakePresets.json `windows-clang-cl-release`, or -Preset;
#      -Lto = windows-clang-cl-release-lto): clang-cl + Ninja through the clang-cl shim
#      (cmake/windows-clang-cl.toolchain.cmake; tools/build/clang_cl_shim.py spells the trees' GNU-style
#      per-target flags for the cl front end), SDL2, the Khronos GL registry headers and FFmpeg from the vcpkg manifest
#      (vcpkg.json: sdl2 + opengl-registry + ffmpeg[avcodec,swscale] -- LGPL features only) through the vcpkg toolchain
#      file chain-loaded by ours. -BuildDir defaults to the preset's binaryDir (build-windows-clang-cl-release[-lto]);
#      -GamesDir (default: this tree's games\, else the submodule's parent's games\) is PS2R_GAMES_DIR; -Disc / -Elf
#      are NATIVEEE_DISC / NATIVEEE_TITLE_ELF (the ELF is extracted, SHA-256 gated and recompiled at build time);
#   2. the component suite (ctest -E the exclusion list);
#   3. stage dist\windows-x64\: nativeee.exe + gs_replay.exe + every DLL vcpkg's applocal deployment placed beside the
#      built exe, the PDBs when present, title\ (the tables the binary reads beside itself: hle\sdk_bindings.tsv,
#      data\post_load_patches.txt, verify\restore_mhash_expected.tsv), BUILD-ID (`nativeee.exe --version`), README.md
#      (the user half), LICENSE, THIRD_PARTY_NOTICES.md + licenses\ (the committed texts + vcpkg's share\<port>\copyright);
#   4. the asset scan over the staged folder (tools/ci/asset_scan.py: zero findings);
#   5. dist\nativeee-windows-x64.zip + .sha256.
#
# Prerequisites: Visual Studio 2022 Build Tools with "C++ Clang tools for Windows" (clang-cl,
# Ninja) -- run from a Developer PowerShell or let the script import VsDevCmd.bat; vcpkg with VCPKG_ROOT; CMake 3.24+;
# Python 3.8+ as `python`; git (the BUILD-ID's commits -- MinGit is enough, no bash needed).
#
# STATUS: the engine's Windows arm (engine/platform/host/host_windows.cpp, engine/platform/windows/: the GL loader
# TwinsanityEvolved's Windows CI compiles and runs under Mesa llvmpipe, the SEH crash handler, the Win32 fibers the IOP
# coroutines use) has been cross-built and LINKED on macOS (nativeee.exe, gs_replay.exe) and compile-checked for
# x86_64-pc-windows-msvc (tools/ci/win_cross_smoke.sh, tools/build/win_cross_smoke.py), not yet RUN on Windows: this
# script runs the component tests right after the build; the first Windows run then does the identity lane
# (WINDOWS.md). The release workflow marks the Windows job continue-on-error until a Windows machine has passed both.
[CmdletBinding()]
param(
    [string]$Disc = "",
    [string]$Elf = "",
    [string]$GamesDir = "",
    [string]$Preset = "",
    [string]$BuildDir = "",
    [string]$DistDir = "",
    [int]$Jobs = 0,
    [switch]$SkipBuild,
    [switch]$NoZip,
    [switch]$Lto,
    [switch]$Help
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\..")).Path
Set-Location $Root
if ($Help) {
    Get-Content $MyInvocation.MyCommand.Path | Select-Object -Skip 0 -First 34 | ForEach-Object { $_ -replace '^# ?', '' }
    exit 0
}
function Step($msg) { Write-Host ""; Write-Host "=== $msg" -ForegroundColor Cyan }
# A native command whose stderr is merged terminates PowerShell 5.1 under Stop on its first stderr line: run such
# commands with the preference relaxed and judge them by $LASTEXITCODE (build.ps1 does the same).
function Invoke-Native([scriptblock]$Block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $global:LASTEXITCODE = 0
    try { & $Block | Out-Host } finally { $ErrorActionPreference = $saved }
    return $global:LASTEXITCODE
}

if (-not $Preset) { $Preset = if ($Lto) { "windows-clang-cl-release-lto" } else { "windows-clang-cl-release" } }
if (-not $BuildDir) {
    # the preset's binaryDir (CMakePresets.json): build-windows-clang-cl-release / -release-lto / -dev
    $presets = Get-Content (Join-Path $Root "CMakePresets.json") -Raw | ConvertFrom-Json
    $p = $presets.configurePresets | Where-Object { $_.name -eq $Preset } | Select-Object -First 1
    if (-not $p) { throw "release_dist.ps1: no configure preset '$Preset' in CMakePresets.json" }
    $bin = $p.binaryDir
    while (-not $bin -and $p.inherits) { $p = $presets.configurePresets | Where-Object { $_.name -eq $p.inherits } | Select-Object -First 1; $bin = $p.binaryDir }
    if (-not $bin) { throw "release_dist.ps1: preset '$Preset' has no binaryDir" }
    $BuildDir = $bin.Replace('${sourceDir}', $Root)
}
if (-not $DistDir) { $DistDir = Join-Path $Root "dist" }
$Platform = "windows-x64"
$Dist = Join-Path $DistDir $Platform
if (-not $GamesDir) {
    # this tree's games\ when it holds a title; else the parent checkout's (the engine as a submodule of a title repository)
    $GamesDir = Join-Path $Root "games"
    $parentGames = Join-Path (Split-Path -Parent $Root) "games"
    $here = @(Get-ChildItem -Directory $GamesDir -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName "hle\CMakeLists.txt") })
    if ($here.Count -eq 0 -and (Test-Path $parentGames)) { $GamesDir = $parentGames }
}
$titles = @(Get-ChildItem -Directory $GamesDir -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName "hle\CMakeLists.txt") })
if ($titles.Count -ne 1) { throw "release_dist.ps1: expected ONE title under $GamesDir (<serial>_<slug>\hle\CMakeLists.txt), found $($titles.Count) -- pass -GamesDir" }
$TitleSrc = $titles[0].FullName

# --- the MSVC developer environment (clang-cl, Ninja, the Windows SDK on PATH) ---------------------------------------
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
        $llvm = Join-Path $vs "VC\Tools\Llvm\x64\bin"
        if ($vs -and (Test-Path (Join-Path $llvm "clang-cl.exe")) -and -not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
            $env:PATH = "$llvm;$env:PATH"
        }
    }
}
foreach ($tool in @("cmake", "ninja", "clang-cl", "python", "git")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "release_dist.ps1: '$tool' not on PATH (prerequisites at the top of this script)" }
}
if (-not $SkipBuild) {
    if (-not $env:VCPKG_ROOT -or -not (Test-Path (Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"))) {
        throw "release_dist.ps1: VCPKG_ROOT must point at a vcpkg checkout (git clone https://github.com/microsoft/vcpkg; .\bootstrap-vcpkg.bat)"
    }
    # The title's inputs: the artefact IS the title binary, whose recompiled code is regenerated from the user's disc.
    $titleArgs = @("-DPS2R_GAMES_DIR=$GamesDir")
    if ($Disc) { $titleArgs += "-DNATIVEEE_DISC=$((Resolve-Path -LiteralPath $Disc).Path)" }
    elseif ($Elf) { $titleArgs += "-DNATIVEEE_TITLE_ELF=$((Resolve-Path -LiteralPath $Elf).Path)" }
    else { throw "release_dist.ps1: no disc image / title ELF given (-Disc X:\path\disc.iso or -Elf X:\path\SCUS_973.16) -- the artefact is the title binary; build.ps1 -NoDisc builds the engine alone" }

    # The preset carries the whole configuration (cmake/windows-clang-cl.toolchain.cmake: clang-cl, the shim, vcpkg); a
    # -BuildDir other than the preset's binaryDir is passed as -B (CMake 3.24+ allows -B with --preset).
    $args = @("--preset", $Preset) + $titleArgs
    if ($BuildDir) { $args += @("-B", $BuildDir) }
    Step "configure (cmake $($args -join ' '))"
    $rc = Invoke-Native { cmake @args }
    if ($rc -ne 0) { throw "release_dist.ps1: configure failed ($rc)" }

    Step "build (engine libs, the recompiled title, nativeee, gs_replay, the component tests)"
    $buildArgs = @("--build", $BuildDir)
    if ($Jobs -gt 0) { $buildArgs += @("-j", "$Jobs") }
    $rc = Invoke-Native { cmake @buildArgs }
    if ($rc -ne 0) { throw "release_dist.ps1: build failed ($rc)" }

    Step "component tests (ctest; the game-file groups and the known exclusions out)"
    $excl = Join-Path $Root "..\.github\ci\ctest_exclusions.txt"
    $ctestArgs = @("--test-dir", $BuildDir, "--output-on-failure", "-E", "^(Root|Og)")
    if (Test-Path $excl) {
        $pat = (Get-Content $excl | Where-Object { $_ -and -not $_.StartsWith("#") }) -join "|"
        if ($pat) { $ctestArgs[-1] = "^(Root|Og)|$pat" }
    }
    $rc = Invoke-Native { ctest @ctestArgs }
    if ($rc -ne 0) { throw "release_dist.ps1: ctest failed ($rc)" }
}

$Bin = Join-Path $BuildDir "app\host\nativeee.exe"
if (-not (Test-Path $Bin)) { throw "release_dist.ps1: no binary at $Bin (build first)" }
$Replay = Join-Path $BuildDir "tools\gs_replay\gs_replay.exe"

Step "stage ($Dist)"
if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
New-Item -ItemType Directory -Force -Path $Dist, (Join-Path $Dist "licenses"), (Join-Path $Dist "title") | Out-Null
Copy-Item $Bin (Join-Path $Dist "nativeee.exe")
if (Test-Path $Replay) { Copy-Item $Replay (Join-Path $Dist "gs_replay.exe") }
# Every DLL vcpkg's applocal deployment placed beside the built exe (SDL2, avcodec, avutil, swscale and their closure).
Get-ChildItem -Path (Split-Path $Bin) -Filter "*.dll" -File | ForEach-Object { Copy-Item $_.FullName (Join-Path $Dist $_.Name) }
foreach ($pdb in @((Join-Path $BuildDir "app\host\nativeee.pdb"), (Join-Path $BuildDir "tools\gs_replay\gs_replay.pdb"))) {
    if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $Dist (Split-Path -Leaf $pdb)) }
}
# The title tables the binary reads beside itself when the source tree is absent (<exe dir>\title).
foreach ($rel in @("hle\sdk_bindings.tsv", "data\post_load_patches.txt", "verify\restore_mhash_expected.tsv")) {
    $src = Join-Path $TitleSrc $rel
    if (Test-Path $src) {
        $dst = Join-Path (Join-Path $Dist "title") $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item $src $dst
    }
}
$engineRev = (git -C $Root rev-parse HEAD).Trim()
$titleRev = (git -C $TitleSrc rev-parse HEAD 2>$null)
if ($titleRev) { $titleRev = $titleRev.Trim() } else { $titleRev = "(not a git tree)" }
$rc = Invoke-Native { & (Join-Path $Dist "nativeee.exe") --version 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding ascii (Join-Path $Dist "BUILD-ID") }
if ($rc -ne 0) { throw "release_dist.ps1: nativeee.exe --version failed (exit $rc) -- a DLL missing beside it?" }
@(
    "engine  $engineRev $((git -C $Root describe --tags --always).Trim())",
    "title   $titleRev ($(Split-Path -Leaf $TitleSrc))",
    "built   $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
) | Add-Content -Encoding ASCII (Join-Path $Dist "BUILD-ID")
Get-Content (Join-Path $Dist "BUILD-ID")
# The user half of README.md: from the top to the first "## Developers" heading.
$readme = Get-Content (Join-Path $Root "README.md")
$cut = [Array]::IndexOf(($readme | ForEach-Object { $_ -match '^## Developers' }), $true)
if ($cut -lt 0) { $cut = $readme.Count }
$readme[0..([Math]::Max($cut - 1, 0))] | Set-Content -Encoding UTF8 (Join-Path $Dist "README.md")
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Dist "LICENSE")
# Licence texts: the committed set + vcpkg's copyright files for the ports.
$licSrc = Join-Path $Root "tools\dist\licenses"
foreach ($f in @("SDL2-LICENSE.txt", "FFmpeg-LICENSE.md", "FFmpeg-COPYING.LGPLv2.1.txt", "imgui-LICENSE.txt", "imgl3w-UNLICENSE.txt")) {
    if (Test-Path (Join-Path $licSrc $f)) { Copy-Item (Join-Path $licSrc $f) (Join-Path $Dist "licenses\$f") }
}
foreach ($port in @("sdl2", "ffmpeg", "opengl-registry")) {
    $cr = Join-Path $BuildDir "vcpkg_installed\x64-windows\share\$port\copyright"
    if (Test-Path $cr) { Copy-Item $cr (Join-Path $Dist "licenses\vcpkg-$port-copyright.txt") }
}
# The FFmpeg DLL must be an LGPL build: the configuration string libavcodec embeds, read from the shipped DLL (or from
# nativeee.exe when FFmpeg was linked statically).
$avcodec = Get-ChildItem -Path $Dist -Filter "avcodec*.dll" -File | Select-Object -First 1
$ffObject = if ($avcodec) { $avcodec } else { Get-Item (Join-Path $Dist "nativeee.exe") }
$bytes = [System.IO.File]::ReadAllBytes($ffObject.FullName)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$conf = @([regex]::Matches($text, '--prefix=[\x20-\x7e]{0,4000}') | ForEach-Object { $_.Value } | Sort-Object -Unique)
if ($conf.Count -ne 1) { Remove-Item -Recurse -Force $Dist; throw "release_dist.ps1: REFUSING to ship -- $($ffObject.Name) embeds $($conf.Count) FFmpeg configuration strings, expected 1" }
if ($conf[0] -match '--enable-gpl|--enable-nonfree|--enable-libx26|--enable-version3') {
    Remove-Item -Recurse -Force $Dist
    throw "release_dist.ps1: REFUSING to ship -- the FFmpeg in $($ffObject.Name) is a GPL/non-free/LGPLv3 build: $($conf[0])"
}
@("FFmpeg as linked ($(if ($avcodec) { "dynamic, $($avcodec.Name)" } else { 'static, nativeee.exe' })).", "", "libavcodec's configuration string embedded in the shipped binary (avcodec_configuration()):", $conf[0]) |
    Set-Content -Encoding UTF8 (Join-Path $Dist "licenses\FFmpeg-BUILD-CONFIGURATION.txt")
@(
    "# NativeEE -- third-party notices -- $((Get-Content (Join-Path $Dist 'BUILD-ID'))[0])",
    "",
    "This folder ships the title program (its code recompiled from YOUR disc at build time) and the NativeEE engine.",
    "It contains NO game data: no textures, audio, models, movies or disc bytes -- you supply your own disc image.",
    "",
    "| component | how it is linked | licence | text |",
    "|---|---|---|---|",
    "| NativeEE (the engine + the title's HLE) | the binary | MIT | LICENSE |",
    "| SDL2 (vcpkg) | dynamic, SDL2.dll | zlib | licenses/SDL2-LICENSE.txt, licenses/vcpkg-sdl2-copyright.txt |",
    "| FFmpeg (libavcodec, libavutil, libswscale; vcpkg, features avcodec + swscale) | dynamic DLLs | LGPL v2.1+ (no GPL / non-free component) | licenses/FFmpeg-LICENSE.md, licenses/FFmpeg-COPYING.LGPLv2.1.txt, licenses/FFmpeg-BUILD-CONFIGURATION.txt |",
    "| Dear ImGui + imgl3w (the in-window menu) | static | MIT / Unlicense | licenses/imgui-LICENSE.txt, licenses/imgl3w-UNLICENSE.txt |",
    "| Khronos OpenGL registry headers (vcpkg opengl-registry) | compile time only (GL/glcorearb.h) | Apache 2.0 / MIT | licenses/vcpkg-opengl-registry-copyright.txt |"
) | Set-Content -Encoding UTF8 (Join-Path $Dist "THIRD_PARTY_NOTICES.md")

Step "asset scan over the staged folder (zero findings)"
$rc = Invoke-Native { & python (Join-Path $Root "tools\ci\asset_scan.py") $Dist }
if ($rc -ne 0) { throw "release_dist.ps1: asset scan found game data in the staged folder ($rc)" }

if (-not $NoZip) {
    Step "zip"
    $zipName = "nativeee-$Platform"
    $zip = Join-Path $DistDir "$zipName.zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }
    $staging = Join-Path $DistDir $zipName
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
    Copy-Item -Recurse $Dist $staging
    Compress-Archive -Path $staging -DestinationPath $zip
    Remove-Item -Recurse -Force $staging
    $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
    "$hash  $zipName.zip" | Set-Content -Encoding ASCII "$zip.sha256"
    Get-Content "$zip.sha256"
}
Write-Host ""
Write-Host "release_dist.ps1: OK -> $Dist"
