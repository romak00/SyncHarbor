<#
.SYNOPSIS
    Installs dependencies on Windows using Chocolatey or vcpkg.
.PARAMETER UseVcpkg
    If set, installs dependencies via vcpkg instead of Chocolatey.
#>
param(
    [switch]$UseVcpkg
)

Write-Host "=== Bootstrap on Windows ==="

if ($UseVcpkg) {
    if (-not (Test-Path "third_party\vcpkg")) {
        git clone https://github.com/microsoft/vcpkg.git third_party\vcpkg
        Push-Location third_party\vcpkg
        .\bootstrap-vcpkg.bat
        Pop-Location
    }
    & third_party\vcpkg\vcpkg install curl sqlite3
}
else {
    choco install git cmake curl sqlite sqlite3 --yes
}

Write-Host "=== Bootstrap complete ==="
