# Windows version of link_mod.sh: copy the Royale mod into the Shipwright-Android submodule.
# Shipwright's CMake uses file(GLOB_RECURSE) without FOLLOW_SYMLINKS, so a link would be skipped. Re-run after editing mod/Royale.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party/Shipwright-Android/soh/soh/Enhancements/Royale"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
Copy-Item -Recurse (Join-Path $root "mod/Royale") $dest
Write-Host "Copied mod/Royale -> $dest (re-run cmake so the glob picks it up)"
