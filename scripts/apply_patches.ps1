# Apply patches/*.patch to the Shipwright-Android submodule. Idempotent: patches that are already applied are skipped.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$fork = Join-Path $root "third_party/Shipwright-Android"
foreach ($p in Get-ChildItem (Join-Path $root "patches") -Filter *.patch | Sort-Object Name) {
    git -C $fork apply --check $p.FullName 2>$null
    if ($LASTEXITCODE -eq 0) { git -C $fork apply $p.FullName; Write-Host "applied $($p.Name)"; continue }
    git -C $fork apply --reverse --check $p.FullName 2>$null
    if ($LASTEXITCODE -eq 0) { Write-Host "already applied: $($p.Name)"; continue }
    throw "$($p.Name) neither applies nor is already applied"
}
