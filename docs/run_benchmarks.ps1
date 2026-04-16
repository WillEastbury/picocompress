param(
    [string]$PythonExe = "python",
    [string]$JsonOut = ""
)

$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found."
}

$install = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) {
    throw "No Visual Studio installation with C++ tools found."
}

$devcmd = Join-Path $install "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devcmd)) {
    throw "VsDevCmd.bat not found."
}

$srcDir = Join-Path $PSScriptRoot "..\src"

$cmd = '"' + $devcmd + '" -arch=x64 -host_arch=x64 >nul && cd /d ' + $srcDir +
    ' && cl /nologo /O2 /LD /TC picocompress.c /link /OUT:picocompress.dll /EXPORT:pc_compress_bound /EXPORT:pc_compress_buffer /EXPORT:pc_decompress_buffer'
cmd /c $cmd
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$args = @("benchmark_harness.py", "--dll", (Join-Path $srcDir "picocompress.dll"))
if ($JsonOut) {
    $args += @("--json-out", $JsonOut)
}

& $PythonExe @args
exit $LASTEXITCODE
