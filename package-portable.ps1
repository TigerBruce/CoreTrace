$ErrorActionPreference = 'Stop'
$outputRoot = Join-Path $PSScriptRoot 'dist'
$packagePath = Join-Path $outputRoot 'CoreTrace-windows-x64.zip'
$hashPath = "$packagePath.sha256"
# Explicit allowlist: never package settings.ini, logs, diagnostics or toolchains.
$files = [ordered]@{
    'CoreTrace.exe' = 'native-portable/CoreTrace.exe'
    'IntelMSR.bin' = 'native-portable/IntelMSR.bin'
    'CoreTrace-transparent.ico' = 'native-portable/CoreTrace-transparent.ico'
    'README.txt' = 'native-portable/README.txt'
    'THIRD-PARTY.txt' = 'native-portable/THIRD-PARTY.txt'
    'LICENSE' = 'LICENSE'
    'NOTICE.md' = 'NOTICE.md'
    'LICENSE-LLVM.txt' = 'native-portable/LICENSE-LLVM.txt'
    'LICENSE-MPL-2.0.txt' = 'native-portable/LICENSE-MPL-2.0.txt'
    'LICENSE-PawnIO-Modules.txt' = 'native-portable/LICENSE-PawnIO-Modules.txt'
}
foreach ($source in $files.Values) {
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $source) -PathType Leaf)) {
        throw "Required package input missing: $source. Run build.cmd first."
    }
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# Create truncates only this known output file; no recursive cleanup is needed.
$stream = [System.IO.File]::Open($packagePath, [System.IO.FileMode]::Create)
try {
    $zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($entry in $files.GetEnumerator()) {
            $sourcePath = Join-Path $PSScriptRoot $entry.Value
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $sourcePath, "CoreTrace/$($entry.Key)", [System.IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    } finally { $zip.Dispose() }
} finally { $stream.Dispose() }
$hash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText($hashPath, "$hash  CoreTrace-windows-x64.zip`n", [System.Text.Encoding]::ASCII)
Write-Host "Portable package: $packagePath"
Write-Host "SHA-256: $hash"
