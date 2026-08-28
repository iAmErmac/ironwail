param([Parameter(Mandatory=$true)][string]$Source,[Parameter(Mandatory=$true)][string]$Output)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=(Resolve-Path -LiteralPath $Source).Path
if(Test-Path -LiteralPath $Output){Remove-Item -LiteralPath $Output -Force}
$zip=[IO.Compression.ZipFile]::Open($Output,[IO.Compression.ZipArchiveMode]::Create)
try { Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {$_.Name -ne 'build_pk3.ps1'} | ForEach-Object {$name=$_.FullName.Substring($root.Length).TrimStart('\','/').Replace('\','/');[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$_.FullName,$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null} } finally {$zip.Dispose()}