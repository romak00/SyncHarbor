<#
.SYNOPSIS
    Configures and builds the project on Windows (Visual Studio 2022 x64).
.PARAMETER Config
    Build configuration: Debug, RelWithDebInfo, or Release.
#>
param(
    [ValidateSet("Debug","RelWithDebInfo","Release")]
    [string]$Config = "Debug"
)

Write-Host "=== Build: $Config ==="
$root = Split-Path $MyInvocation.MyCommand.Definition -Parent
Push-Location $root

if (-not (Test-Path build)) { New-Item build -ItemType Directory | Out-Null }
Push-Location build

$tk = ""
if (Test-Path "../third_party/vcpkg") {
    $tk = "-DCMAKE_TOOLCHAIN_FILE=../third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"
}

cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=$Config $tk
cmake --build . --config $Config --parallel

Pop-Location
Pop-Location
Write-Host "=== Build done ==="
