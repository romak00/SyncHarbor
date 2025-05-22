<#
.SYNOPSIS
    Configures and builds the project on Windows (Visual Studio 2022 x64).
.PARAMETER Config
    Build configuration: Debug, RelWithDebInfo, or Release.
.PARAMETER AdditionalCMakeArgs
    All additional cmake flags.
#>
param(
    [ValidateSet("Debug","RelWithDebInfo","Release")]
    [string]$Config = "Debug",

    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$AdditionalCMakeArgs
)

Write-Host "=== Build: $Config ==="

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $projectRoot

if (-not (Test-Path build)) {
    New-Item build -ItemType Directory | Out-Null
}
Push-Location build

$cmakeArgs = @(
    ".."
    "-G", "Visual Studio 17 2022"
    "-A", "x64"
    "-DCMAKE_BUILD_TYPE=$Config"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
) + $AdditionalCMakeArgs

Write-Host "`n=> cmake $($cmakeArgs -join ' ')`n"
cmake @cmakeArgs

Write-Host "`n=> cmake --build . --config $Config --parallel`n"
cmake --build . --config $Config --parallel

Pop-Location
Pop-Location

Write-Host "=== Build done ==="
