[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = "Stop"

# This helper is intentionally local-only: IronRift embeds a separate Ironwail checkout.
$sourceRoot = "D:\game_dev\ironwail"
$destinationRoot = "D:\game_dev\IronRift\Projects\Android\jni\ironwail"

$bridgeFiles = @(
    "Quake\android_gles.c",
    "Quake\android_gles.h",
    "Quake\android_lifecycle.c",
    "Quake\android_lifecycle.h",
    "Quake\cl_input.c",
    "Quake\cl_tent.c",
    "Quake\client.h",
    "Quake\gl_shaders.c",
    "Quake\gl_refrag.c",
    "Quake\gl_shaders.h",
    "Quake\gl_vidsdl.c",
    "Quake\host_cmd.c",
    "Quake\keys.c",
    "Quake\keys.h",
    "Quake\menu.c",
    "Quake\pr_cmds.c",
    "Quake\pr_edict.c",
    "Quake\progs.h",
    "Quake\render.h",
    "Quake\xr_bridge.h",
    "Quake\xr_input.c",
    "Quake\xr_input.h",
    "Quake\xr_interaction.c",
    "Quake\xr_interaction.h",
    "Quake\sv_phys.c"
)

$rendererMultiviewFiles = @(
    "Quake\gl_rlight.c",
    "Quake\gl_rmain.c",
    "Quake\gl_screen.c",
    "Quake\gl_sky.c",
    "Quake\glquake.h",
    "Quake\r_alias.c",
    "Quake\r_part.c",
    "Quake\r_sprite.c",
    "Quake\r_world.c",
    "Quake\view.c"
)

if (-not (Test-Path -LiteralPath $destinationRoot)) {
    throw "IronRift embedded renderer checkout was not found: $destinationRoot"
}

$files = @($bridgeFiles + $rendererMultiviewFiles)

foreach ($relativePath in $files) {
    $source = Join-Path $sourceRoot $relativePath
    $destination = Join-Path $destinationRoot $relativePath
    if (-not (Test-Path -LiteralPath $source)) { throw "Source file was not found: $source" }
    if (-not (Test-Path -LiteralPath $destination)) { throw "Destination file was not found: $destination" }
    if ($PSCmdlet.ShouldProcess($destination, "Copy $relativePath from Ironwail")) {
        Copy-Item -LiteralPath $source -Destination $destination -Force
        (Get-Item -LiteralPath $destination).LastWriteTime = Get-Date
    }
}

Write-Output "Synced $($files.Count) file(s) to the IronRift embedded checkout."
