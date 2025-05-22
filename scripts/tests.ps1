<#
.SYNOPSIS
    Runs ctest from the build directory.
.PARAMETER Config
    Build configuration: Debug, RelWithDebInfo, or Release.
#>
param(
    [ValidateSet("Debug","RelWithDebInfo","Release")]
    [string]$Config = "Debug"
)

Write-Host "=== Test: $Config ==="
Push-Location build
ctest --output-on-failure --parallel 4 -C $Config
Pop-Location
Write-Host "=== Tests complete ==="
