# =============================================================================
#  make_single_exe.ps1 -- DLConsole as ONE .exe for Windows 10/11 (session 269)
#  ---------------------------------------------------------------------------
#  The release DLConsole.exe and everything it needs (Qt DLLs and plugins
#  from windeployqt, the MSVC runtime) packed into a single program. Started,
#  it unpacks to a temporary folder, runs DLConsole from there, waits, and
#  deletes the folder when DLConsole closes. Nothing is installed.
#
#  The unpacker is 7-Zip's small SFX module (7zS2.sfx, LZMA SDK, public
#  domain), BUILT HERE FROM SOURCE with two changes:
#    - DLConsole's icon instead of 7-Zip's setup icon (the operator sees the
#      unpacker's icon, not DLConsole.exe's: it is the file they start);
#    - it sets DLCONSOLE_PACKAGE to its own path, so DLConsole looks for the
#      files "beside the program" (color_rules.json, fieldmap.json, ...)
#      beside the single .exe, not in the temporary folder (programdir.cpp).
#
#  Needs: an MSVC developer environment (nmake, cl, rc), windeployqt of the
#  Qt the program was built with on PATH, 7z.exe, and internet access for the
#  LZMA SDK (fixed version, checked against its SHA-256).
#
#      packaging\windows\make_single_exe.ps1 -Exe build\DLConsole.exe -Out dist\DLConsole.exe
# =============================================================================
param(
    [Parameter(Mandatory = $true)] [string] $Exe,
    [Parameter(Mandatory = $true)] [string] $Out,
    [string] $Work = "package-work"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $here "..\..")

$sdkUrl    = "https://www.7-zip.org/a/lzma2301.7z"
$sdkSha256 = "317dd834d6bbfd95433488b832e823cd3d4d420101436422c03af88507dd1370"

# Absolute paths: .NET's current folder is not PowerShell's.
function Full($p) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($p) }
$Exe = Full $Exe; $Out = Full $Out; $Work = Full $Work

function Check($what) { if ($LASTEXITCODE -ne 0) { throw "$what failed ($LASTEXITCODE)" } }

if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
$stage = Join-Path $Work "stage"
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# ---- 1. The program and what it needs --------------------------------------
Copy-Item $Exe (Join-Path $stage "DLConsole.exe")
windeployqt --release --no-translations --no-compiler-runtime (Join-Path $stage "DLConsole.exe")
Check "windeployqt"

# The MSVC runtime, app-local: Windows 10/11 do not always have it, and
# windeployqt's alternative (vc_redist.x64.exe) is an installer, which is
# what this package is meant to avoid.
$crt = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" |
       Select-Object -First 1
if (-not $crt) { throw "MSVC runtime not found under $env:VCToolsRedistDir\x64" }
Copy-Item (Join-Path $crt.FullName "*.dll") $stage

# 7zS2 runs the best-ranked program at the top of the archive: a .bat or
# .cmd outranks an .exe. DLConsole.exe must be the only candidate.
$others = Get-ChildItem $stage -File | Where-Object {
    $_.Extension -in ".bat", ".cmd", ".exe", ".inf", ".msi", ".htm", ".html" -and $_.Name -ne "DLConsole.exe" }
if ($others) { throw "files the unpacker could start instead of DLConsole.exe: $($others.Name -join ', ')" }

# ---- 2. The unpacker, from source -------------------------------------------
$sdkArc = Join-Path $Work "lzma.7z"
Invoke-WebRequest -Uri $sdkUrl -OutFile $sdkArc -UseBasicParsing
$got = (Get-FileHash -Algorithm SHA256 $sdkArc).Hash.ToLower()
if ($got -ne $sdkSha256) { throw "LZMA SDK checksum mismatch: $got" }
$sdk = Join-Path $Work "lzma"
7z x -y "-o$sdk" $sdkArc | Out-Null
Check "7z x (LZMA SDK)"

$sfxDir = Join-Path $sdk "C\Util\SfxSetup"
Get-ChildItem -Recurse $sdk | ForEach-Object { $_.Attributes = $_.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly) }

$src = Join-Path $sfxDir "SfxSetup.c"
$c = [IO.File]::ReadAllText($src)
$anchor = "      SetCurrentDirectory(workCurDir);`r`n    }`r`n    #endif`r`n"
if (-not $c.Contains($anchor)) { throw "SfxSetup.c: the place for DLCONSOLE_PACKAGE was not found (LZMA SDK changed?)" }
$add = "`r`n    /* DLConsole: where the single .exe the operator started is. */`r`n" +
       "    SetEnvironmentVariableW(L`"DLCONSOLE_PACKAGE`", sfxPath);`r`n"
$c = $c.Replace($anchor, $anchor + $add)
[IO.File]::WriteAllText($src, $c)
Copy-Item -Force (Join-Path $repo "app\dlconsole.ico") (Join-Path $sfxDir "setup.ico")

Push-Location $sfxDir
nmake /nologo PLATFORM=x64
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { throw "building 7zS2.sfx failed ($rc)" }
$sfx = Get-ChildItem -Recurse $sfxDir -Filter "7zS2.sfx" | Select-Object -First 1
if (-not $sfx) { throw "7zS2.sfx was not built" }

# ---- 3. Archive + unpacker = one program ------------------------------------
# LZMA2 with the x86 BCJ filter: methods 7zS2 can decode.
$arc = Join-Path $Work "payload.7z"
7z a -t7z -mx=9 -m0=BCJ -m1=LZMA2 -ms=on $arc (Join-Path $stage "*") | Out-Null
Check "7z a (payload)"

$outDir = Split-Path -Parent $Out
if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$fs = [IO.File]::Create($Out)
foreach ($part in $sfx.FullName, $arc) {
    $in = [IO.File]::OpenRead($part); $in.CopyTo($fs); $in.Close()
}
$fs.Close()
Write-Host ("{0}: {1:N1} MB ({2} files packed)" -f $Out, ((Get-Item $Out).Length / 1MB),
            (Get-ChildItem -Recurse -File $stage).Count)
