param()

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'cl.exe not found. Run from a Visual Studio Developer shell or initialise VsDevCmd first.'
}

$Dist = Join-Path $Root 'dist'
New-Item -ItemType Directory -Force -Path $Dist | Out-Null

function Invoke-Cl([string[]]$CompilerArgs) {
    & cl.exe @CompilerArgs
    if ($LASTEXITCODE -ne 0) { throw "cl.exe failed with exit code $LASTEXITCODE" }
}

$Common = @('/nologo','/O2','/W4','/std:c11','/Iinclude')

# Host/registry DLL. No codec implementation is linked into it.
Invoke-Cl ($Common + @('/DPCX_HOST_BUILD','/LD','src\host.c',"/Fe:$Dist\picocompress_host.dll",'/link',"/IMPLIB:$Dist\picocompress_host.lib"))

# Native v3 micro codec as an independently loadable DLL.
Invoke-Cl ($Common + @('/Isrc','/LD','src\picocompress.c','modules\micro\picocompress_micro.c',"/Fe:$Dist\picocodec_micro.dll"))

# Thin shell linked to the host import library only.
Invoke-Cl ($Common + @('src\cli.c',"$Dist\picocompress_host.lib","/Fe:$Dist\picocompress.exe"))

# Registry fallback test, compiled statically against host.c.
Invoke-Cl ($Common + @('/DPCX_HOST_STATIC','tests\test_modular_host.c','src\host.c',"/Fe:$Dist\test_modular_host.exe"))
& "$Dist\test_modular_host.exe"
if ($LASTEXITCODE -ne 0) { throw 'modular host test failed' }

# Preserve native codec regression suites.
Invoke-Cl ($Common + @('/Isrc','src\picocompress.c','src\test_picocompress.c',"/Fe:$Dist\test_picocompress.exe"))
& "$Dist\test_picocompress.exe"
if ($LASTEXITCODE -ne 0) { throw 'native codec regression failed' }

Invoke-Cl ($Common + @('/Isrc','src\picocompress.c','src\test_picocompress_additional.c',"/Fe:$Dist\test_picocompress_additional.exe"))
& "$Dist\test_picocompress_additional.exe"
if ($LASTEXITCODE -ne 0) { throw 'native codec additional regression failed' }

# Prove the shell discovers the DLL dynamically and round-trips through it.
$env:PICOCOMPRESS_CODEC_PATH = $Dist
$list = & "$Dist\picocompress.exe" list
if ($LASTEXITCODE -ne 0 -or -not ($list -match '^micro\s')) { throw 'micro codec was not dynamically discovered' }

$InputPath = Join-Path $Dist 'roundtrip.txt'
$PackedPath = Join-Path $Dist 'roundtrip.pc'
$OutputPath = Join-Path $Dist 'roundtrip.out'
[IO.File]::WriteAllText($InputPath, "picocompress modular codec roundtrip: alpha beta beta beta`n")
& "$Dist\picocompress.exe" compress micro $InputPath $PackedPath
if ($LASTEXITCODE -ne 0) { throw 'shell compression failed' }
& "$Dist\picocompress.exe" decompress micro $PackedPath $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'shell decompression failed' }

$before = [IO.File]::ReadAllBytes($InputPath)
$after = [IO.File]::ReadAllBytes($OutputPath)
if ($before.Length -ne $after.Length -or [Convert]::ToBase64String($before) -ne [Convert]::ToBase64String($after)) {
    throw 'shell roundtrip bytes differ'
}

Write-Host 'picocompress modular build: ok'
