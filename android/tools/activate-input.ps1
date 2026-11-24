param(
    [string]$Serial,
    [string]$Adb = "$env:LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Adb)) {
    $sdk = (Get-Content -LiteralPath "$PSScriptRoot/../local.properties" | Where-Object { $_ -match '^sdk.dir=' } | Select-Object -First 1) -replace '^sdk.dir=', ''
    $sdk = $sdk.Replace('\:', ':').Replace('\\', '\')
    $Adb = Join-Path $sdk 'platform-tools/adb.exe'
}
if (!(Test-Path -LiteralPath $Adb)) { throw 'ADB not found. Pass -Adb with the full adb.exe path.' }
$adbArgs = @()
if ($Serial) { $adbArgs = @('-s', $Serial) }
function Invoke-Adb([string[]]$Arguments) {
    $result = & $Adb @adbArgs @Arguments
    if ($LASTEXITCODE -ne 0) { throw "ADB failed: $($Arguments[0])" }
    return $result
}
$null = Invoke-Adb @('get-state')
$null = Invoke-Adb @('shell', 'appops', 'set', 'com.deskconnect.app', 'SYSTEM_ALERT_WINDOW', 'allow')
$null = Invoke-Adb @('shell', 'am', 'start', '-n', 'com.deskconnect.app/.ui.MainActivity', '--ez', 'enable_input', 'false')
Start-Sleep -Seconds 1
# run-as intentionally requires the locally deployed debug build. Never print credentials.
$configText = (Invoke-Adb @('shell', 'run-as', 'com.deskconnect.app', 'cat', 'files/input-bridge.json')) -join "`n"
$config = $configText | ConvertFrom-Json
if ($config.port -lt 1024 -or $config.port -gt 65535 -or $config.token -notmatch '^[0-9a-f]{64}$') { throw 'Invalid input bridge configuration.' }
$apkLine = Invoke-Adb @('shell', 'pm', 'path', 'com.deskconnect.app') | Where-Object { $_ -match '/base.apk$' } | Select-Object -First 1
$apkPath = ($apkLine -replace '^package:', '').Trim()
if ($apkPath -notmatch '^/data/app/[a-zA-Z0-9_./=+~-]+/base\.apk$') { throw 'Cannot locate installed APK.' }
$remoteDir = '/data/local/tmp/deskconnect-input'
$localDir = Join-Path ([IO.Path]::GetTempPath()) ('deskconnect-input-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $localDir
$configPath = Join-Path $localDir 'bootstrap.json'
$scriptPath = Join-Path $localDir 'start.sh'
try {
    [IO.File]::WriteAllText($configPath, $configText, [Text.UTF8Encoding]::new($false))
    $script = @'
#!/system/bin/sh
set -e
cd /data/local/tmp/deskconnect-input
if [ -f helper.pid ]; then
    old_pid=$(cat helper.pid)
    case "$old_pid" in
        ''|*[!0-9]*) ;;
        *) if [ -r "/proc/$old_pid/cmdline" ] && tr '\000' ' ' < "/proc/$old_pid/cmdline" | grep -q 'com.deskconnect.app.input.ShellInputMain'; then kill "$old_pid"; sleep 1; fi ;;
    esac
fi
export CLASSPATH='__APK__'
nohup /system/bin/app_process / com.deskconnect.app.input.ShellInputMain /data/local/tmp/deskconnect-input/bootstrap.json </dev/null >helper.log 2>&1 &
echo $! >helper.pid
'@
    [IO.File]::WriteAllText($scriptPath, $script.Replace('__APK__', $apkPath).Replace("`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $null = Invoke-Adb @('shell', 'mkdir', '-p', $remoteDir)
    $null = Invoke-Adb @('shell', 'chmod', '700', $remoteDir)
    $null = Invoke-Adb @('push', $configPath, "$remoteDir/bootstrap.json")
    $null = Invoke-Adb @('push', $scriptPath, "$remoteDir/start.sh")
    $null = Invoke-Adb @('shell', 'chmod', '600', "$remoteDir/bootstrap.json")
    $null = Invoke-Adb @('shell', 'sh', "$remoteDir/start.sh")
    Start-Sleep -Seconds 2
    $helperLog = (Invoke-Adb @('shell', 'cat', "$remoteDir/helper.log")) -join "`n"
    if ($helperLog -notmatch 'input helper ready') { throw "Helper failed to start: $helperLog" }
    $null = Invoke-Adb @('shell', 'am', 'start', '-n', 'com.deskconnect.app/.ui.MainActivity', '--ez', 'enable_input', 'true')
    Write-Host 'ADB input helper activated. Confirm the app shows ADB enhanced mode ready.'
} finally {
    # Only exact files created in our unique temporary directory are removed.
    if (Test-Path -LiteralPath $configPath) { Remove-Item -LiteralPath $configPath -Force }
    if (Test-Path -LiteralPath $scriptPath) { Remove-Item -LiteralPath $scriptPath -Force }
    Remove-Item -LiteralPath $localDir -Force
}
