<#
  JamShield - deploy the IoT layer (PWA + MQTT WebSockets + optional cloud) to
  the Raspberry Pi. Run this AFTER the Pi base setup (Mosquitto + receiver).

  Usage:
    .\deploy_iot.ps1                       # deploy PWA + WS listener + PWA host
    .\deploy_iot.ps1 -Cloud                # also install the cloud bridge
                                           # (edit scripts/jamshield-cloud.conf first)
  Env: set JS_RPI_PASS to the Pi password for the sudo steps.
#>
param([switch]$Cloud)
$ErrorActionPreference = "Stop"
$ROOT = "C:\Workspace\IotELL"
$RPI  = "$ROOT\scripts\rpi.py"
if (-not $env:JS_RPI_HOST) { $env:JS_RPI_HOST = "prathvi.local" }
if (-not $env:JS_RPI_PASS) { Write-Host "Set `$env:JS_RPI_PASS first (Pi password)." -ForegroundColor Red; exit 1 }

Write-Host "[iot] packaging + pushing PWA..." -ForegroundColor Yellow
& "$env:WINDIR\System32\tar.exe" -czf "$env:TEMP\jspwa.tgz" -C "$ROOT\dashboard" pwa
python $RPI putfile "$env:TEMP\jspwa.tgz"            /tmp/jspwa.tgz
python $RPI run "mkdir -p ~/jamshield && tar xzf /tmp/jspwa.tgz -C ~/jamshield && echo pwa-extracted"

Write-Host "[iot] pushing configs + setup script..." -ForegroundColor Yellow
python $RPI putfile "$ROOT\scripts\mosquitto_ws.conf"  /tmp/mosquitto_ws.conf
python $RPI putfile "$ROOT\scripts\rpi_iot_setup.sh"   /tmp/rpi_iot_setup.sh
if ($Cloud -and (Test-Path "$ROOT\scripts\jamshield-cloud.conf")) {
  python $RPI putfile "$ROOT\scripts\jamshield-cloud.conf" /tmp/jamshield-cloud.conf
  Write-Host "      (cloud bridge config pushed)" -ForegroundColor Green
}
python $RPI run "tr -d '\r' < /tmp/rpi_iot_setup.sh > /tmp/iot.sh"

Write-Host "[iot] running setup on the Pi (sudo)..." -ForegroundColor Yellow
python $RPI sudobash /tmp/iot.sh

Write-Host "[iot] done. PWA + MQTT-WS live on the Pi." -ForegroundColor Cyan
