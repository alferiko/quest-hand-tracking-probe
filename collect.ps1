param([string]$Destination, [string]$Serial, [string]$Adb = 'adb')
$ErrorActionPreference = 'Stop'
if (!$Destination) { $Destination = Join-Path $PSScriptRoot ('captures/' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
if (Test-Path -LiteralPath $Destination) { throw 'Choose a new destination to preserve earlier captures' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$deviceArgs = @()
if ($Serial) { $deviceArgs = @('-s', $Serial) }
& $Adb @deviceArgs pull /sdcard/Android/data/io.github.alferiko.handprobe/files "$Destination/device-files"
if ($LASTEXITCODE -ne 0) { throw 'ADB pull failed' }
Write-Output "Saved: $Destination"
Write-Output 'Run summarize.py on a captured CSV for source counts and controller overlap.'
