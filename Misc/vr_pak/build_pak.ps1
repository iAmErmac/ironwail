param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Output
)

$sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$files = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object { $_.Name -ne "build_pak.ps1" -and $_.Extension -ne ".pak" } | Sort-Object FullName)
$entries = @()
$offset = 12
foreach ($file in $files) {
    $relative = $file.FullName.Substring($sourceRoot.Length).TrimStart('\', '/').Replace('\', '/')
    $nameBytes = [Text.Encoding]::ASCII.GetBytes($relative)
    if ($nameBytes.Length -gt 55) { throw "PAK path is too long: $relative" }
    $data = [IO.File]::ReadAllBytes($file.FullName)
    $entries += [pscustomobject]@{ Name = $relative; Data = $data; Offset = $offset }
    $offset += $data.Length
}

$directoryOffset = $offset
$directoryLength = 64 * $entries.Count
$outputDir = Split-Path -Parent $Output
if ($outputDir) { New-Item -ItemType Directory -Force -Path $outputDir | Out-Null }
$stream = [IO.File]::Create($Output)
$writer = New-Object IO.BinaryWriter($stream)
$writer.Write([Text.Encoding]::ASCII.GetBytes('PACK'))
$writer.Write([int]$directoryOffset)
$writer.Write([int]$directoryLength)
foreach ($entry in $entries) { $writer.Write($entry.Data) }
foreach ($entry in $entries) {
    $name = New-Object byte[] 56
    $bytes = [Text.Encoding]::ASCII.GetBytes($entry.Name)
    [Array]::Copy($bytes, $name, $bytes.Length)
    $writer.Write($name)
    $writer.Write([int]$entry.Offset)
    $writer.Write([int]$entry.Data.Length)
}
$writer.Close()
$stream.Close()
