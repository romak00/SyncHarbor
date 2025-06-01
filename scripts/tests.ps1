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

Write-Host "Running DatabaseUnitTests in parallel..."
ctest `
    --output-on-failure `
    --parallel 4 `
    -C $Config `
    -R "^DatabaseUnitTest\."

Write-Host "Running LocalStorageUnitTests sequentially..."
ctest `
  --output-on-failure `
  -C $Config `
  -R "^LocalStorageUnitTests\."

Write-Host "Running HttpServerUnitTests sequentially..."
ctest `
  --output-on-failure `
  -C $Config `
  -R "^HttpServerUnitTest\."

Write-Host "Running CallbackDispatcherUnitTests in parallel..."
ctest `
  --output-on-failure `
  --parallel 4 `
  -C $Config `
  -R "^CallbackDispatcherUnitTest\."

Write-Host "Running RequestHandleUnitTests in parallel..."
ctest `
  --output-on-failure `
  --parallel 4 `
  -C $Config `
  -R "^RequestHandleUnitTest\."

Write-Host "Running ActiveCountUnitTests sequentially..."
ctest `
  --output-on-failure `
  -C $Config `
  -R "^ActiveCountUnitTest\."

Write-Host "Running EventRegistryUnitTests sequentially..."
ctest `
  --output-on-failure `
  -C $Config `
  -R "^EventRegistryUnitTest\."

Write-Host "Running UtilsUnitTests in parallel..."
ctest `
  --output-on-failure `
  --parallel 4 `
  -C $Config `
  -R "^UtilsUnitTest\."

Write-Host "Running SyncManagerUnitTests sequentially..."
ctest `
  --output-on-failure `
  -C $Config `
  -R "^SyncManagerUnitTest\."

Write-Host "Running HttpClientIntegrationTests sequentially..."
ctest `
  --output-on-failure `
  -C $Config `
  -R "^HttpClientIntegrationTest\."

Write-Host "Running LocalStorageIntegrationTests sequentially..."
ctest `
    --output-on-failure `
    -C $Config `
    -R "^LocalStorageIntegrationTest\."

Pop-Location
Write-Host "=== Tests complete ==="
