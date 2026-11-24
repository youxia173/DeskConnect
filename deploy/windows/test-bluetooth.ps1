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
    # QtTest stdout may be unavailable on Windows. Read its explicit report,
    # and retain the exit code/import list if the process cannot start at all.
    $report = Join-Path $build 'BluetoothTests.txt'
    $process = Start-Process -FilePath $test -ArgumentList @('-o', ('"' + $report + ',txt"')) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'BluetoothTests diagnostic run timed out' }
    Write-Host "BluetoothTests native exit code: $($process.ExitCode)"
    Get-Content -LiteralPath $report -Encoding utf8 -ErrorAction Continue
    dumpbin /dependents $test
    Get-ChildItem -LiteralPath (Split-Path $test) -Filter '*.dll' | Select-Object Name, Length
    Get-Content -LiteralPath "$build/src/unittests/Testing/Temporary/LastTest.log" -ErrorAction Continue
}
exit $testResult
