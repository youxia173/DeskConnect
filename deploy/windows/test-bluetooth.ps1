param([Parameter(Mandatory = $true)][string]$BuildDirectory)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$test = Join-Path $build 'src/unittests/gui/core/BluetoothTests.exe'
$qtBin = Join-Path $env:QT_ROOT_DIR 'bin'
$env:PATH = "$qtBin;$build\bin;$env:PATH"

# The application is deployed into bin; CTest executables have their own folders.
# Deploy the test's actual Qt dependencies (including Qt6Test and the CRT) there.
& (Join-Path $qtBin 'windeployqt.exe') --release --no-translations $test
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir "$build/src/unittests" --output-on-failure --verbose --no-tests=error -R '^BluetoothTests$'
$testResult = $LASTEXITCODE
if ($testResult) {
    # CTest can report an empty failure when Windows cannot start the process.
    # Preserve the native exit code and import list to diagnose that case.
    & $test -o -,txt
    Write-Host "BluetoothTests native exit code: $LASTEXITCODE"
    dumpbin /dependents $test
    Get-ChildItem -LiteralPath (Split-Path $test) -Filter '*.dll' | Select-Object Name, Length
    Get-Content -LiteralPath "$build/src/unittests/Testing/Temporary/LastTest.log" -ErrorAction Continue
}
exit $testResult
