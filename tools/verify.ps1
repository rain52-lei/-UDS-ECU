param([string]$Python = $env:PYTHON)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:TMP = $out
$env:TEMP = $out
$env:PYTHONDONTWRITEBYTECODE = '1'
if (!$Python) {
    $candidates = @('C:\Users\Administrator\AppData\Local\Programs\Python\Python39\python.exe',
        'C:\Users\Administrator\AppData\Local\Programs\Python\Python312\python.exe')
    $Python = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (!$Python) { $Python = (Get-Command python.exe -ErrorAction Stop).Source }
}
$log = Join-Path $out 'verification.txt'
Start-Transcript -LiteralPath $log -Force | Out-Null
try {
    & (Join-Path $PSScriptRoot 'build.ps1') -HostOnly
    & $Python -B (Join-Path $root 'tests\test_can_client.py')
    if ($LASTEXITCODE -ne 0) { throw 'CAN client tests failed' }
    & $Python -B (Join-Path $root 'host\panel.py') --self-test
    if ($LASTEXITCODE -ne 0) { throw 'Panel self-test failed' }
    Write-Output 'All software verification checks passed.'
} finally {
    Stop-Transcript | Out-Null
}
