# tools/toolchain/fetch_compiler.ps1 -- the Windows twin of fetch_compiler.sh: the pinned official LLVM kit for
# windows-x86_64 (tools/toolchain/COMPILER_PINS.sha256), downloaded (cached), sha256-VERIFIED, cut to the slim subset the
# release ships (clang++.exe, clang-cl.exe, lld-link.exe, llvm-lib.exe, llvm-ar.exe, lib\clang\<major>\include, LICENSE.TXT), COMPILER.json
# written, optionally smoked (clang-cl compiles + lld-link links a one-file program against the installed Windows SDK /
# MSVC CRT -- the kit does not and cannot carry those) and zipped.
#
#   powershell -ExecutionPolicy Bypass -File tools\toolchain\fetch_compiler.ps1 -Out DIR [-Cache DIR] [-Pack] [-Smoke]
#
# Windows 10+ ships tar.exe (bsdtar) which reads .tar.xz; PowerShell 5.1+ has Get-FileHash and Compress-Archive.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Cache = $env:NATIVEEE_TOOLCHAIN_CACHE,
    [switch]$Pack,
    [switch]$Smoke
)
$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Pins = Join-Path $Here "COMPILER_PINS.sha256"
function Step($t) { Write-Host ""; Write-Host "=== $t" }

$row = Get-Content $Pins | Where-Object { $_ -notmatch '^\s*#' -and $_ -match '^([0-9a-f]{64})\s+(clang\+llvm-[0-9.]+-x86_64-pc-windows-msvc\.tar\.xz)$' } | Select-Object -First 1
if (-not $row) { throw "fetch_compiler: no windows-x86_64 row in $Pins" }
$null = $row -match '^([0-9a-f]{64})\s+(\S+)$'; $WantSha = $Matches[1]; $Kit = $Matches[2]
$Version = [regex]::Match($Kit, '[0-9]+\.[0-9]+\.[0-9]+').Value; $Major = $Version.Split('.')[0]
$Url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$Version/$Kit"
Write-Host "platform windows-x86_64  kit $Kit  llvm $Version  sha256 $WantSha"
New-Item -ItemType Directory -Force -Path $Out | Out-Null; $Out = (Resolve-Path $Out).Path
if (-not $Cache) { $Cache = Join-Path $Out ".cache" }
New-Item -ItemType Directory -Force -Path $Cache | Out-Null; $Cache = (Resolve-Path $Cache).Path
$File = Join-Path $Cache $Kit

Step "the pinned kit ($File)"
$ok = (Test-Path $File) -and ((Get-FileHash $File -Algorithm SHA256).Hash.ToLower() -eq $WantSha)
if ($ok) { Write-Host "cached and verified" } else {
    if (Test-Path $File) { Remove-Item $File }
    Write-Host "downloading $Url"
    # curl.exe (Windows 10+) resumes and retries; Invoke-WebRequest would hold 900 MB in memory
    & curl.exe -fSL --retry 3 -o "$File.part" $Url
    if ($LASTEXITCODE -ne 0) { Remove-Item "$File.part" -ErrorAction SilentlyContinue; throw "fetch_compiler: download failed" }
    Move-Item "$File.part" $File
    $got = (Get-FileHash $File -Algorithm SHA256).Hash.ToLower()
    if ($got -ne $WantSha) { Remove-Item $File; throw "fetch_compiler: sha256 MISMATCH for $Kit -- got $got, pinned $WantSha. Deleted; refusing." }
    Write-Host "downloaded and verified"
}

Step "listing the kit (cached beside it)"
$Members = "$File.members"
if (-not (Test-Path $Members) -or (Get-Item $Members).Length -eq 0) { & tar.exe -tf $File | Set-Content "$Members.part" -Encoding UTF8; Move-Item "$Members.part" $Members }
$all = Get-Content $Members
$Top = ($all | Select-Object -First 1).Split('/')[0]
Write-Host "top folder: $Top  ($($all.Count) members)"
$WantRe = "^$([regex]::Escape($Top))/(bin/(clang\+\+\.exe|clang-cl\.exe|lld-link\.exe|llvm-lib\.exe|llvm-ar\.exe)$|lib/clang/$Major/include(/|$)|LICENSE\.TXT$)"
$cut = $all | Where-Object { $_ -match $WantRe }
if ($cut.Count -eq 0) { throw "fetch_compiler: nothing matched in $Kit" }
Write-Host "cutting $($cut.Count) members"
$Dest = Join-Path $Out "compiler"
if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
$list = Join-Path $Out ".cut_members.txt"; $cut | Set-Content $list -Encoding ASCII
Step "extract -> $Dest"
& tar.exe -xf $File -C $Dest --strip-components=1 -T $list
if ($LASTEXITCODE -ne 0) { throw "fetch_compiler: tar failed" }
Get-ChildItem (Join-Path $Dest "bin") | ForEach-Object { Write-Host $_.Name }

$files = Get-ChildItem $Dest -Recurse -File | Where-Object { $_.Name -ne "COMPILER.json" } | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($Dest.Length + 1).Replace('\', '/'); size = $_.Length; symlink = $null } }
$doc = [ordered]@{
    format = 1; llvm = $Version; platform = "windows-x86_64"
    kit = [ordered]@{ name = $Kit; sha256 = $WantSha; url = $Url }
    bin = [ordered]@{ cc = "bin/clang++.exe"; cxx = "bin/clang++.exe"; ld = "bin/lld-link.exe"; ar = "bin/llvm-lib.exe" }
    resource_dir = "lib/clang/$Major"
    licence = "LICENSE.TXT (Apache License v2.0 with LLVM Exceptions)"
    system_sdk_required = "the Windows SDK and the MSVC C runtime headers/libraries (Visual Studio Build Tools)"
    cut_by = "engine/tools/toolchain/fetch_compiler.ps1"
    members = @($cut); files = @($files)
}
$doc | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $Dest "COMPILER.json") -Encoding UTF8
Remove-Item $list
$mb = [math]::Round((($files | Measure-Object size -Sum).Sum) / 1MB, 1)
Write-Host "COMPILER.json: $($files.Count) files, $mb MB unpacked"

if ($Smoke) {
    Step "smoke: clang-cl compiles and lld-link links a one-file program (needs the Windows SDK on this machine)"
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("nativeee-smoke-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    @'
#include <cstdio>
#include <cstdint>
#include <vector>
int main() { std::vector<std::uint32_t> v{1, 2, 3}; std::printf("nativeee compiler smoke: %zu\n", v.size()); return 0; }
'@ | Set-Content (Join-Path $tmp "hello.cpp") -Encoding ASCII
    & (Join-Path $Dest "bin\clang-cl.exe") --version | Select-Object -First 1
    & (Join-Path $Dest "bin\clang-cl.exe") /O2 /std:c++17 /EHsc (Join-Path $tmp "hello.cpp") /Fe:(Join-Path $tmp "hello.exe") "-fuse-ld=lld"
    if ($LASTEXITCODE -ne 0) { throw "fetch_compiler: smoke compile/link failed (is the Windows SDK installed? run from a Developer PowerShell)" }
    $outp = & (Join-Path $tmp "hello.exe")
    if ($outp -notmatch "compiler smoke: 3") { throw "fetch_compiler: smoke program printed '$outp'" }
    Write-Host "smoke: ok (compiled with clang-cl, linked with lld-link)"
    Remove-Item -Recurse -Force $tmp
}

if ($Pack) {
    Step "pack (compiler-windows-x86_64.tar.xz; top folder compiler/) -- the name tools/prepare reads"
    $Name = "compiler-windows-x86_64"
    $Archive = Join-Path $Out "$Name.tar.xz"
    if (Test-Path $Archive) { Remove-Item $Archive }
    # tar.exe (bsdtar, Windows 10+) writes xz through libarchive
    & tar.exe -cJf $Archive -C $Out compiler
    if ($LASTEXITCODE -ne 0) { throw "fetch_compiler: tar failed" }
    $sha = (Get-FileHash $Archive -Algorithm SHA256).Hash.ToLower()
    "$sha  $Name.tar.xz" | Set-Content "$Archive.sha256" -Encoding ASCII
    Write-Host "archive: $Archive ($([math]::Round((Get-Item $Archive).Length / 1MB)) MB)  sha256 $sha"
}
Write-Host ""; Write-Host "fetch_compiler: OK -> $Dest"
