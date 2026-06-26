<#
  JamShield — one-shot launcher.

  Does everything: auto-detects which ESP32 is the node vs jammer (robust to USB
  port swaps), detects the Pi/broker IP, patches the firmware, builds BOTH
  firmwares (node + jammer), flashes them, and opens the live dashboard.

  Usage:
    .\run.ps1                       # full: detect ports+IP, build, flash both, dashboard
    .\run.ps1 -SkipBuild            # skip build+flash, just detect + open the dashboard
    .\run.ps1 -Node COM6 -Jammer COM14   # force specific ports (skips auto-detect)
    .\run.ps1 -NoJammer             # node only (no 2nd ESP32 connected)
#>
param(
  [string]$Node     = "",
  [string]$Jammer   = "",
  [string]$EspNowRx = "",   # ESP-NOW receiver board (Arduino sketch); starts the bridge
  [string]$PiIP     = "",   # override Pi/broker IP if mDNS (prathvi.local) fails
  [int]   $Port     = 8080,
  [switch]$SkipBuild,
  [switch]$NoJammer
)
$ErrorActionPreference = "Stop"
$ROOT = "C:\Workspace\IotELL"
$HDR  = "$ROOT\src\esp32\include\jamshield.h"
$RPI  = "$ROOT\scripts\rpi.py"

function WslBash($cmd) { wsl.exe -d Ubuntu -- bash -lc $cmd }
function Flash($port,$dir) {
  python -m esptool --chip esp32 -p $port -b 460800 write_flash --flash_size detect `
    0x1000 "$dir\bootloader.bin" 0x8000 "$dir\partition-table.bin" 0x10000 "$dir\zephyr.bin"
}

Write-Host "==================  JamShield launcher  ==================" -ForegroundColor Cyan

# 0) Auto-detect which CP210x board is the node vs the jammer (robust to the
#    two ESP32s swapping USB COM ports between sessions).
Write-Host "[0/5] Auto-detecting ESP32 ports..." -ForegroundColor Yellow
try {
  $d = python "$ROOT\scripts\detect_ports.py" | ConvertFrom-Json
  if (-not $Node     -and $d.node)       { $Node     = $d.node }
  if (-not $Jammer   -and $d.jammer)     { $Jammer   = $d.jammer }
  if (-not $EspNowRx -and $d.espnow_rx)  { $EspNowRx = $d.espnow_rx }
  Write-Host "      node=$Node  jammer=$Jammer  espnow_rx=$EspNowRx" -ForegroundColor Green
} catch { Write-Host "      auto-detect failed; falling back to defaults" -ForegroundColor DarkYellow }
if (-not $Node) { $Node = "COM6" }
# 2nd board is the ESP-NOW receiver (Arduino), not a jammer -> don't flash a jammer.
if ($EspNowRx -and -not $Jammer) { $NoJammer = $true }

if (-not $SkipBuild) {
  # 1) Detect the Pi / broker IP (best effort via mDNS + SSH key) -----------
  Write-Host "[1/5] Detecting Raspberry Pi (broker) IP..." -ForegroundColor Yellow
  if ($PiIP) {
    $piip = $PiIP
    Write-Host "      Using -PiIP override = $piip" -ForegroundColor Green
  } else {
    $env:JS_RPI_HOST = "prathvi.local"
    $piout = ""
    try { $piout = python $RPI run "hostname -I" 2>$null } catch {}
    $env:JS_RPI_HOST = $null
    $piip = ($piout -split '\s+' | Where-Object { $_ -match '^(10|192|172)\.\d+\.\d+\.\d+$' } | Select-Object -First 1)
  }
  if ($piip) {
    Write-Host "      Pi/broker IP = $piip" -ForegroundColor Green
    $txt = Get-Content $HDR -Raw
    $new = [regex]::Replace($txt, 'JS_MQTT_BROKER_IP\s+"[^"]*"', "JS_MQTT_BROKER_IP   `"$piip`"")
    if ($new -ne $txt) { Set-Content $HDR $new -NoNewline; Write-Host "      Patched broker IP in jamshield.h" }
  } else {
    Write-Host "      Could not detect Pi IP (mDNS down?) - keeping current value in jamshield.h" -ForegroundColor DarkYellow
  }

  # 1b) Detect the victim AP's 2.4 GHz channel and patch the jammer ----------
  if (-not $NoJammer) {
    try {
      $blocks = (netsh wlan show networks mode=bssid | Out-String) -split "`r`n`r`n"
      foreach ($b in $blocks) {
        if ($b -match ': Loki\b') {
          $chs = [regex]::Matches($b, 'Channel\s*:\s*(\d+)') | ForEach-Object { [int]$_.Groups[1].Value }
          $c24 = $chs | Where-Object { $_ -le 14 } | Select-Object -First 1
          if ($c24) {
            Write-Host "      Loki 2.4GHz channel = $c24 - patching jammer" -ForegroundColor Green
            $jm = "$ROOT\src\jammer\src\main.c"
            (Get-Content $jm -Raw) -replace '(#define TARGET_CHANNEL\s+)\d+', "`${1}$c24" | Set-Content $jm -NoNewline
            break
          }
        }
      }
    } catch { Write-Host "      (channel auto-detect skipped)" -ForegroundColor DarkYellow }
  }

  # 2) Build node firmware ---------------------------------------------------
  Write-Host "[2/5] Building node firmware..." -ForegroundColor Yellow
  WslBash "bash ~/jamshield_bootstrap/build.sh auto" | Select-Object -Last 1
  WslBash "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/copy_flash.sh | bash" | Out-Null

  # 3) Build jammer firmware -------------------------------------------------
  if (-not $NoJammer -and $Jammer) {
    Write-Host "[3/5] Building jammer firmware..." -ForegroundColor Yellow
    WslBash "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/build_jammer.sh > /tmp/bj.sh; bash /tmp/bj.sh auto" | Select-Object -Last 1
    WslBash "tr -d '\r' < /mnt/c/Workspace/IotELL/scripts/copy_flash_jammer.sh | bash" | Out-Null
  } else { Write-Host "[3/5] Skipping jammer" }

  # 4) Flash boards ----------------------------------------------------------
  Write-Host "[4/5] Flashing node -> $Node ..." -ForegroundColor Yellow
  Flash $Node "$ROOT\flash"
  if (-not $NoJammer -and $Jammer) {
    Write-Host "      Flashing jammer -> $Jammer ..." -ForegroundColor Yellow
    Flash $Jammer "$ROOT\flash\jammer"
  }
  Start-Sleep 2
} else {
  Write-Host "[--] SkipBuild: going straight to the dashboard" -ForegroundColor DarkYellow
}

# 5) ESP-NOW bridge (if a receiver board is present) ------------------------
if ($EspNowRx) {
  $bip = if ($piip) { $piip } elseif ($PiIP) { $PiIP } else { "10.182.210.137" }
  Write-Host "[5/6] Starting ESP-NOW bridge: $EspNowRx -> $bip ..." -ForegroundColor Yellow
  Start-Process python -ArgumentList "$ROOT\scripts\espnow_bridge.py", $EspNowRx, $bip
}

# 6) Dashboard --------------------------------------------------------------
Write-Host "[6/6] Starting dashboard at http://127.0.0.1:$Port/ ..." -ForegroundColor Yellow
$jarg = if ($NoJammer -or -not $Jammer) { "COM_NONE" } else { $Jammer }
python "$ROOT\dashboard\dashboard.py" --node $Node --jammer $jarg --port $Port
