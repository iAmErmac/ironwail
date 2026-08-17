param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string]$Files
)
$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$entries = @()
$offset = 12
foreach ($relative in ($Files -split ";")) {
    $normalized = $relative.Replace('\', '/')
    $nameBytes = [Text.Encoding]::ASCII.GetBytes($normalized)
    if ($nameBytes.Length -gt 55) { throw "PAK path is too long: $normalized" }
    $path = Join-Path $sourceRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing PAK input: $relative" }
    $data = [IO.File]::ReadAllBytes($path)
    $entries += [pscustomobject]@{ Name = $normalized; Data = $data; Offset = $offset }
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