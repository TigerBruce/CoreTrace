$ErrorActionPreference = 'Stop'
$toolRoot = Join-Path $PSScriptRoot 'tools'
$toolName = 'llvm-mingw-20261020-ucrt-x86_64'
$compiler = Join-Path $toolRoot "$toolName/bin/clang++.exe"
if (Test-Path -LiteralPath $compiler) { Write-Host "Toolchain ready: $compiler"; exit 0 }
New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
$archivePath = Join-Path $toolRoot "$toolName.zip"
$downloadUrl = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip'
$expectedHash = 'fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd'
if (!(Test-Path -LiteralPath $archivePath)) {
  Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath -UseBasicParsing
}
$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) { throw 'Toolchain SHA256 mismatch; do not use this archive.' }
Expand-Archive -LiteralPath $archivePath -DestinationPath $toolRoot -Force
if (!(Test-Path -LiteralPath $compiler)) { throw 'Compiler missing after extraction.' }
Remove-Item -LiteralPath $archivePath
Write-Host 'Toolchain ready. Run build.cmd after closing CoreTrace.'
