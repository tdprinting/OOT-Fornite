# Apply our patches to the Shipwright-Android submodule and to its nested libultraship submodule.
#   patches/*.patch              -> third_party/Shipwright-Android
#   patches/libultraship/*.patch -> third_party/Shipwright-Android/libultraship
# Idempotent: patches that are already applied are skipped.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function Apply-Dir($repo, $dir) {
    if (-not (Test-Path $dir)) { return }
    foreach ($p in Get-ChildItem $dir -Filter *.patch | Sort-Object Name) {
        git -C $repo apply --check $p.FullName 2>$null
        if ($LASTEXITCODE -eq 0) { git -C $repo apply $p.FullName; Write-Host "applied $($p.Name) to $(Split-Path -Leaf $repo)"; continue }
        git -C $repo apply --reverse --check $p.FullName 2>$null
        if ($LASTEXITCODE -eq 0) { Write-Host "already applied: $($p.Name)"; continue }
        throw "$($p.Name) neither applies nor is already applied"
    }
}
Apply-Dir (Join-Path $root "third_party/Shipwright-Android") (Join-Path $root "patches")
Apply-Dir (Join-Path $root "third_party/Shipwright-Android/libultraship") (Join-Path $root "patches/libultraship")
