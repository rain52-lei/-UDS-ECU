param([switch]$HostOnly, [switch]$ExternalCan, [switch]$Watchdog)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:TMP = $out
$env:TEMP = $out
function Run($exe, $arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "Tool failed: $exe ($LASTEXITCODE)" }
}
$hostCompiler = if ($env:HOST_GCC) { $env:HOST_GCC } else { 'G:\S32D\S32DS\build_tools\msys32\mingw32\bin\gcc.exe' }
$env:PATH = (Split-Path $hostCompiler -Parent) + ';' + $env:PATH
$pure = @('can_protocol','remote_control','diag_store','isotp','uds','scheduler_guard','faults','control','ecu_state','temperature')
$pureSources = @($pure | ForEach-Object { Join-Path $root "src\$_.c" })
$flags = @('-std=c99','-Wall','-Wextra','-Werror','-pedantic','-O2',"-I$root\include")
Run $hostCompiler ($flags + @('-static') + $pureSources + @("$root\tests\test_core.c",'-o',"$out\test_core.exe"))
Run "$out\test_core.exe" @()
Run $hostCompiler ($flags + @('-static',"-I$root\SDK\platform\devices") + $pureSources + @(
    "$root\src\app.c","$root\src\serial_command.c","$root\tests\test_app.c",'-o',"$out\test_app.exe"))
Run "$out\test_app.exe" @()
Run $hostCompiler ($flags + @('-static',"$root\src\control.c","$root\tests\test_model.c",'-o',"$out\test_model.exe",'-lm'))
Run "$out\test_model.exe" @("$out\thermal-model.csv")
Run $hostCompiler ($flags + @('-static',"-I$root\tests\fakes","-I$root\SDK\platform\devices",
    "$root\src\serial_port.c","$root\tests\test_serial_port.c",'-o',"$out\test_serial_port.exe"))
Run "$out\test_serial_port.exe" @()
if ($HostOnly) { return }

$variant = if ($ExternalCan) { 'external' } else { 'loopback' }
if ($Watchdog) { $variant += '-watchdog' } else { $variant += '-debug' }
$out = Join-Path $out $variant
New-Item -ItemType Directory -Force -Path $out | Out-Null

$arm = if ($env:S32K_GCC) { $env:S32K_GCC } else { 'G:\S32D\S32DS\build_tools\gcc_v6.3\gcc-6.3-arm32-eabi\bin\arm-none-eabi-gcc.exe' }
$bin = Split-Path $arm -Parent
$runtime = Join-Path (Split-Path $bin -Parent) 'arm-none-eabi\newlib'
$env:PATH = $bin + ';' + $env:PATH
$incs = @('include','Generated_Code','Sources','SDK/platform/drivers/src/clock/S32K1xx',
    'SDK/platform/devices','SDK/platform/devices/common','SDK/platform/devices/S32K144/include',
    'SDK/platform/devices/S32K144/startup','SDK/platform/drivers/inc','SDK/platform/drivers/src/ftm',
    'SDK/rtos/osif','SDK/platform/pal/inc')
$common = @('-std=c99','-Wall','-Wextra','-Werror','-pedantic','-O1','-g','-mcpu=cortex-m4','-mthumb',
    '-mfloat-abi=hard','-mfpu=fpv4-sp-d16','-ffunction-sections','-fdata-sections','-fno-common','-fstack-usage',
    '-DCPU_S32K144HFT0VLLT','-DCPU_S32K144','-DTURN_ON_CPU0','-DSTART_FROM_FLASH',"--sysroot=$runtime")
$common += @($incs | ForEach-Object { '-I' + (Join-Path $root $_) })
if ($ExternalCan) { $common += '-DTG_CAN_LOOPBACK=0' }
if ($Watchdog) { $common += '-DTG_WATCHDOG_ENABLE=1' }
$sourceDirs = @('src','Generated_Code','Sources','SDK')
$sources = @($sourceDirs | ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $root $_) -Filter '*.c' -File -Recurse })
$sources += Get-Item -LiteralPath "$root\Project_Settings\Startup_Code\startup_S32K144.S"
$objects = @()
foreach ($source in $sources) {
    $rel = $source.FullName.Substring($root.Length + 1)
    $object = Join-Path $out ($rel + '.o')
    New-Item -ItemType Directory -Force -Path (Split-Path $object -Parent) | Out-Null
    Run $arm ($common + @('-c',$source.FullName,'-o',$object))
    $objects += $object
}
$elf = Join-Path $out 'ThermoGuard_SDK.elf'
$link = @('-mcpu=cortex-m4','-mthumb','-mfloat-abi=hard','-mfpu=fpv4-sp-d16',"--sysroot=$runtime",
    '-specs=nano.specs','-specs=nosys.specs','-Wl,--gc-sections',"-Wl,-Map,$out\ThermoGuard_SDK.map",'-n',
    '-T',"$root\Project_Settings\Linker_Files\S32K144_64_flash.ld")
Run $arm ($link + $objects + @('-o',$elf))
Run (Join-Path $bin 'arm-none-eabi-size.exe') @($elf)
Run (Join-Path $bin 'arm-none-eabi-objcopy.exe') @('-O','ihex',$elf,"$out\ThermoGuard_SDK.hex")
Run (Join-Path $bin 'arm-none-eabi-objcopy.exe') @('-O','binary',$elf,"$out\ThermoGuard_SDK.bin")
$info = "Variant=$variant`nExternalCan=$ExternalCan`nWatchdog=$Watchdog`nBuilt=" + (Get-Date -Format o) + "`n"
[System.IO.File]::WriteAllText((Join-Path $out 'build-info.txt'), $info, [System.Text.UTF8Encoding]::new($false))
Write-Output "ARM build: $elf; ExternalCan=$ExternalCan Watchdog=$Watchdog"
