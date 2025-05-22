<#
.SYNOPSIS
    Runs ctest from the build directory, splitting DatabaseTests и LocalStorageTests.
.PARAMETER Config
    Build configuration: Debug, RelWithDebInfo, or Release.
#>
param(
    [ValidateSet("Debug","RelWithDebInfo","Release")]
    [string]$Config = "Debug"
)

Write-Host "=== Test: $Config ==="
Push-Location build

Write-Host "Running DatabaseTests in parallel…"
ctest `
    --output-on-failure `
    --parallel 4 `
    -C $Config `
    -R "^DatabaseTest\."

Write-Host "Running LocalStorageTests sequentially…"
ctest `
    --output-on-failure `
    -C $Config `
    -R "^LocalStorageTest\."

Pop-Location
Write-Host "=== Tests complete ==="
